// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Intelligent Predictive Asset Streaming & Memory Budgeting Engine
#include "spm/spm_streaming.h"
#include "spm/spm_neural_assist.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

struct InternalAsset {
    SpmAssetDescriptor desc;
    bool isQueued{false};
    bool isResident{false};
    uint64_t lastAccessTime{0};
};

struct StreamingContext {
    uint64_t totalBudgetBytes{0};
    uint32_t maxUploadPerFrameBytes{0};
    std::atomic<bool> running{false};

    SpmCameraState camera{};
    mutable std::mutex cameraMutex;

    mutable std::mutex assetMutex;
    std::unordered_map<uint64_t, InternalAsset> assets;

    mutable std::mutex queueMutex;
    std::deque<uint64_t> priorityQueues[SPM_PRIORITY_COUNT];

    std::vector<std::thread> workers;
    std::atomic<uint32_t> activeWorkers{0};

    std::atomic<uint64_t> currentAllocatedBytes{0};
    std::atomic<uint64_t> pendingQueueBytes{0};
    std::atomic<uint64_t> uploadedThisFrameBytes{0};
    std::atomic<uint32_t> evictedCount{0};

    int directStorageAvailable{0};
    std::atomic<float> dynamicLookaheadMeters{100.0f};
    std::atomic<float> dynamicPriorityMultiplier{1.0f};
    std::atomic<float> proactiveLodBias{0.0f};
    std::atomic<uint32_t> neuralInferenceLatencyUs{0};
    std::atomic<int> neuralAccelerated{0};
};

static StreamingContext* s_ctx = nullptr;
static std::mutex s_initMutex;

static float Vec3Dot(const SpmVector3& a, const SpmVector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float Vec3Length(const SpmVector3& v) {
    return std::sqrt(Vec3Dot(v, v));
}

static SpmVector3 Vec3Sub(const SpmVector3& a, const SpmVector3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

static void ProbeDirectStorage(StreamingContext* ctx) {
#ifdef _WIN32
    HMODULE hDStorage = LoadLibraryW(L"dstorage.dll");
    if (hDStorage) {
        ctx->directStorageAvailable = 1;
        FreeLibrary(hDStorage);
    } else {
        ctx->directStorageAvailable = 0;
    }
#else
    ctx->directStorageAvailable = 0;
#endif
}

static void StreamingWorkerThread(StreamingContext* ctx) {
    ctx->activeWorkers++;
    while (ctx->running) {
        uint64_t assetToLoad = 0;
        {
            std::lock_guard<std::mutex> lock(ctx->queueMutex);
            for (int p = 0; p < SPM_PRIORITY_COUNT; ++p) {
                if (!ctx->priorityQueues[p].empty()) {
                    assetToLoad = ctx->priorityQueues[p].front();
                    ctx->priorityQueues[p].pop_front();
                    break;
                }
            }
        }

        if (assetToLoad == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // Simulate asynchronous staging buffer I/O and preparation
        std::this_thread::sleep_for(std::chrono::microseconds(200));

        {
            std::lock_guard<std::mutex> lock(ctx->assetMutex);
            auto it = ctx->assets.find(assetToLoad);
            if (it != ctx->assets.end()) {
                it->second.isQueued = false;
                // Stage ready for gradual time-sliced GPU upload
            }
        }
    }
    ctx->activeWorkers--;
}

} // namespace

int SPM_StreamingInit(uint64_t vramBudgetMb, uint32_t maxUploadMbPerFrame, uint32_t workerCount) {
    std::lock_guard<std::mutex> initLock(s_initMutex);
    if (s_ctx) {
        return 1; // already initialized
    }

    s_ctx = new StreamingContext();
    uint64_t budgetMb = (vramBudgetMb == 0) ? 1720 : vramBudgetMb;
    s_ctx->totalBudgetBytes = budgetMb * 1024ULL * 1024ULL;

    uint32_t uploadMb = (maxUploadMbPerFrame == 0) ? 3 : maxUploadMbPerFrame;
    s_ctx->maxUploadPerFrameBytes = uploadMb * 1024ULL * 1024ULL;

    ProbeDirectStorage(s_ctx);

    s_ctx->running = true;
    uint32_t workers = (workerCount == 0) ? 2 : std::clamp(workerCount, 1U, 8U);
    for (uint32_t i = 0; i < workers; ++i) {
        s_ctx->workers.emplace_back(StreamingWorkerThread, s_ctx);
    }

    return 1;
}

void SPM_StreamingShutdown(void) {
    std::lock_guard<std::mutex> initLock(s_initMutex);
    if (!s_ctx) return;

    s_ctx->running = false;
    for (auto& w : s_ctx->workers) {
        if (w.joinable()) {
            w.join();
        }
    }
    s_ctx->workers.clear();

    delete s_ctx;
    s_ctx = nullptr;
}

void SPM_StreamingUpdateCamera(const SpmCameraState* camera) {
    if (!s_ctx || !camera) return;
    {
        std::lock_guard<std::mutex> lock(s_ctx->cameraMutex);
        s_ctx->camera = *camera;
    }

    // Feed player movement into SPM Neural Assist
    SpmPredictorInput input{};
    input.playerPosX = camera->position.x;
    input.playerPosY = camera->position.y;
    input.playerPosZ = camera->position.z;
    input.playerVelX = camera->velocity.x;
    input.playerVelY = camera->velocity.y;
    input.playerVelZ = camera->velocity.z;
    input.cameraForwardX = camera->forward.x;
    input.cameraForwardY = camera->forward.y;
    input.cameraForwardZ = camera->forward.z;
    input.fovDegrees = camera->fovDegrees;
    input.currentVramUsedMb = (uint32_t)(s_ctx->currentAllocatedBytes.load() / (1024ULL * 1024ULL));
    input.currentVramBudgetMb = (uint32_t)(s_ctx->totalBudgetBytes / (1024ULL * 1024ULL));

    uint32_t queueCount = 0;
    {
        std::lock_guard<std::mutex> qLock(s_ctx->queueMutex);
        for (int p = 0; p < SPM_PRIORITY_COUNT; ++p) {
            queueCount += static_cast<uint32_t>(s_ctx->priorityQueues[p].size());
        }
    }
    input.pendingLoadQueueSize = queueCount;

    SpmPredictorOutput output{};
    if (SPM_NeuralAssistPredict(&input, &output)) {
        s_ctx->dynamicLookaheadMeters.store(output.recommendedLookaheadDistance);
        s_ctx->dynamicPriorityMultiplier.store(output.prefetchPriorityMultiplier);
        s_ctx->proactiveLodBias.store(output.proactiveLodBias);
        s_ctx->neuralInferenceLatencyUs.store(output.inferenceTimeMicroseconds);
        s_ctx->neuralAccelerated.store(SPM_NeuralAssistIsAccelerated());
    }
}

int SPM_StreamingRegisterAsset(const SpmAssetDescriptor* desc) {
    if (!s_ctx || !desc) return 0;
    std::lock_guard<std::mutex> lock(s_ctx->assetMutex);
    InternalAsset item{};
    item.desc = *desc;
    s_ctx->assets[desc->assetId] = item;
    return 1;
}

SpmStreamPriority SPM_StreamingEvaluatePriority(const SpmAssetDescriptor* asset) {
    if (!s_ctx || !asset) return SPM_PRIORITY_BACKGROUND;

    SpmCameraState cam{};
    {
        std::lock_guard<std::mutex> lock(s_ctx->cameraMutex);
        cam = s_ctx->camera;
    }

    // Compute center of bounding box
    SpmVector3 center = {
        (asset->boundsMin.x + asset->boundsMax.x) * 0.5f,
        (asset->boundsMin.y + asset->boundsMax.y) * 0.5f,
        (asset->boundsMin.z + asset->boundsMax.z) * 0.5f
    };

    SpmVector3 toAsset = Vec3Sub(center, cam.position);
    float dist = Vec3Length(toAsset);
    if (dist < 0.001f) dist = 0.001f;

    SpmVector3 dirToAsset = { toAsset.x / dist, toAsset.y / dist, toAsset.z / dist };

    // Camera view frustum cone
    float forwardDot = Vec3Dot(dirToAsset, cam.forward);

    // Velocity lookahead cone (predictive prefetch)
    float speed = Vec3Length(cam.velocity);
    float velDot = 0.0f;
    if (speed > 0.5f) {
        SpmVector3 velDir = { cam.velocity.x / speed, cam.velocity.y / speed, cam.velocity.z / speed };
        velDot = Vec3Dot(dirToAsset, velDir);
    }

    // 1. Immediately visible and critical close proximity
    if (asset->isVisible || (dist <= 25.0f && forwardDot > 0.45f)) {
        return SPM_PRIORITY_CRITICAL;
    }

    // 2. Velocity-projected trajectory anticipation
    float maxLookahead = s_ctx->dynamicLookaheadMeters.load();
    if (speed > 1.0f && velDot > 0.60f && dist <= (speed * 2.5f + (maxLookahead * 0.35f))) {
        return SPM_PRIORITY_PREDICTED;
    }

    // 3. Medium distance within view angle
    if (dist <= maxLookahead && forwardDot > 0.20f) {
        return SPM_PRIORITY_NORMAL;
    }

    // 4. Distant background
    return SPM_PRIORITY_BACKGROUND;
}

int SPM_StreamingQueueRequest(uint64_t assetId, SpmStreamPriority priority) {
    if (!s_ctx) return 0;
    if (priority < 0 || priority >= SPM_PRIORITY_COUNT) {
        priority = SPM_PRIORITY_NORMAL;
    }

    std::lock_guard<std::mutex> lock(s_ctx->assetMutex);
    auto it = s_ctx->assets.find(assetId);
    if (it == s_ctx->assets.end()) return 0;

    if (it->second.isQueued || it->second.isResident) return 1;

    it->second.isQueued = true;
    s_ctx->pendingQueueBytes += it->second.desc.sizeBytes;

    std::lock_guard<std::mutex> qLock(s_ctx->queueMutex);
    s_ctx->priorityQueues[priority].push_back(assetId);
    return 1;
}

uint32_t SPM_StreamingProcessTimeSlice(uint32_t maxBytesThisFrame) {
    if (!s_ctx) return 0;

    uint32_t quota = (maxBytesThisFrame == 0) ? s_ctx->maxUploadPerFrameBytes : maxBytesThisFrame;
    uint32_t bytesUploaded = 0;

    std::lock_guard<std::mutex> lock(s_ctx->assetMutex);
    for (auto& kvp : s_ctx->assets) {
        if (bytesUploaded >= quota) break;

        InternalAsset& a = kvp.second;
        if (!a.isResident && a.isQueued) {
            uint32_t slice = (a.desc.sizeBytes > (quota - bytesUploaded))
                ? (quota - bytesUploaded)
                : a.desc.sizeBytes;

            a.desc.residentBytes += slice;
            bytesUploaded += slice;
            s_ctx->currentAllocatedBytes += slice;
            if (s_ctx->pendingQueueBytes >= slice) {
                s_ctx->pendingQueueBytes -= slice;
            }

            if (a.desc.residentBytes >= a.desc.sizeBytes) {
                a.isResident = true;
                a.isQueued = false;
            }
        }
    }

    s_ctx->uploadedThisFrameBytes = bytesUploaded;
    return bytesUploaded;
}

int SPM_StreamingEnforceBudget(void) {
    if (!s_ctx) return 0;

    uint64_t current = s_ctx->currentAllocatedBytes.load();
    uint64_t limit = s_ctx->totalBudgetBytes;
    if (limit == 0) return 0; // unlimited

    uint64_t criticalWatermark = (limit * 88ULL) / 100ULL;
    uint64_t targetPressure    = (limit * 75ULL) / 100ULL;

    if (current < criticalWatermark) {
        return 0; // under critical watermark
    }

    std::lock_guard<std::mutex> lock(s_ctx->assetMutex);

    // Evict non-visible resident assets starting from lowest priority
    int evicted = 0;
    for (auto& kvp : s_ctx->assets) {
        if (s_ctx->currentAllocatedBytes.load() <= targetPressure) break;

        InternalAsset& a = kvp.second;
        if (a.isResident && !a.desc.isVisible) {
            uint32_t freed = a.desc.residentBytes;
            a.desc.residentBytes = 0;
            a.isResident = false;
            if (s_ctx->currentAllocatedBytes >= freed) {
                s_ctx->currentAllocatedBytes -= freed;
            } else {
                s_ctx->currentAllocatedBytes = 0;
            }
            s_ctx->evictedCount++;
            evicted++;
        }
    }

    return evicted;
}

void SPM_StreamingGetStats(SpmStreamingStats* outStats) {
    if (!outStats) return;
    memset(outStats, 0, sizeof(*outStats));
    if (!s_ctx) return;

    outStats->totalBudgetBytes = s_ctx->totalBudgetBytes;
    outStats->currentAllocatedBytes = s_ctx->currentAllocatedBytes.load();
    outStats->pendingQueueBytes = s_ctx->pendingQueueBytes.load();
    outStats->uploadedThisFrameBytes = s_ctx->uploadedThisFrameBytes.load();
    outStats->activeWorkers = s_ctx->activeWorkers.load();
    outStats->evictedCount = s_ctx->evictedCount.load();
    outStats->directStorageAvailable = s_ctx->directStorageAvailable;
    outStats->dynamicLookaheadMeters = s_ctx->dynamicLookaheadMeters.load();
    outStats->dynamicPriorityMultiplier = s_ctx->dynamicPriorityMultiplier.load();
    outStats->proactiveLodBias = s_ctx->proactiveLodBias.load();
    outStats->neuralInferenceLatencyUs = s_ctx->neuralInferenceLatencyUs.load();
    outStats->neuralAccelerated = s_ctx->neuralAccelerated.load();

    std::lock_guard<std::mutex> lock(s_ctx->queueMutex);
    uint32_t queued = 0;
    for (int p = 0; p < SPM_PRIORITY_COUNT; ++p) {
        queued += static_cast<uint32_t>(s_ctx->priorityQueues[p].size());
    }
    outStats->queuedRequests = queued;
}
