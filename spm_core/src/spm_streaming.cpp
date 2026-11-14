// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Intelligent Predictive Asset Streaming & Memory Budgeting Engine
//
// The engine never invents asset data or VRAM residency. Asset bytes only
// enter the process through the read callback supplied by the runtime, and
// only reach the renderer through the upload callback. When no runtime is
// connected the module reports ioConnected/uploadConnected = 0 and leaves
// requests queued instead of pretending that work happened.
#include "spm/spm_streaming.h"
#include "spm/spm_neural_assist.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

// Read callbacks are invoked in chunks so a large asset can be cancelled
// between them and does not need one giant read.
constexpr uint32_t kReadChunkBytes = 1024u * 1024u;
constexpr uint64_t kVramAlignment = 256ull;
constexpr uint32_t kMaxWorkers = 8;
constexpr uint64_t kDefaultHysteresisFrames = 150ull;

struct VramRange {
    uint64_t offset;
    uint64_t size;
};

enum class AssetState : uint8_t {
    Registered = 0,
    Queued,
    Reading,
    Staged,
    Resident,
    Failed
};

struct InternalAsset {
    SpmAssetDescriptor desc{};
    AssetState state{AssetState::Registered};
    uint32_t requestPriority{SPM_PRIORITY_NORMAL};
    SpmVisibilityState visibilityState{SPM_VISIBILITY_INACTIVE};
    bool pinned{false};
    bool hasVram{false};
    bool hasExplicitVisibility{false};
    uint64_t vramOffset{0};
    uint64_t lastVisibleFrame{0};
    uint64_t gpuFenceValue{0};
    float priorityWeight{1.0f};
    uint32_t uploadedBytes{0};
    // Kept across evictions so a re-stream of the same asset does not have to
    // allocate the staging buffer again.
    std::vector<uint8_t> staging;

    InternalAsset* lruPrev{nullptr};
    InternalAsset* lruNext{nullptr};
    InternalAsset* queuePrev{nullptr};
    InternalAsset* queueNext{nullptr};
    InternalAsset* stagedPrev{nullptr};
    InternalAsset* stagedNext{nullptr};
};

struct StreamingContext {
    uint64_t totalBudgetBytes{0};
    uint64_t backingBudgetBytes{0};
    uint64_t osAvailableBudgetBytes{0};
    uint64_t criticalWatermark{0};
    uint64_t targetWatermark{0};
    uint64_t currentFrameIndex{0};
    uint64_t hysteresisFrames{kDefaultHysteresisFrames};
    uint64_t submittedGpuFence{0};
    uint64_t completedGpuFence{0};
    bool isIntegratedGpu{false};
    uint32_t maxUploadPerFrameBytes{0};
    uint32_t configuredWorkers{0};

    std::atomic<bool> running{false};

    mutable std::mutex mutex;
    std::condition_variable queueCv;

    SpmCameraState camera{};
    bool hasCamera{false};

    std::unordered_map<uint64_t, InternalAsset> assets;

    InternalAsset* queueHead[SPM_PRIORITY_COUNT]{};
    InternalAsset* queueTail[SPM_PRIORITY_COUNT]{};
    uint32_t queueCount[SPM_PRIORITY_COUNT]{};
    uint32_t queuedTotal{0};

    InternalAsset* stagedHead[SPM_PRIORITY_COUNT]{};
    InternalAsset* stagedTail[SPM_PRIORITY_COUNT]{};
    uint32_t stagedCount[SPM_PRIORITY_COUNT]{};
    uint32_t stagedTotal{0};

    InternalAsset* lruHead{nullptr};
    InternalAsset* lruTail{nullptr};
    uint32_t residentCount{0};

    std::vector<VramRange> vramFree;

    std::thread workers[kMaxWorkers];
    std::atomic<uint32_t> activeWorkers{0};

    SpmAssetReadCallback readCallback{nullptr};
    SpmAssetUploadCallback uploadCallback{nullptr};
    SpmAssetReleaseCallback releaseCallback{nullptr};
    void* callbackUserData{nullptr};

    // Serializes time-sliced uploads so two render threads cannot upload the
    // same staged asset twice.
    std::mutex uploaderMutex;

    std::atomic<uint64_t> currentAllocatedBytes{0};
    std::atomic<uint64_t> pendingQueueBytes{0};
    std::atomic<uint64_t> stagedBytes{0};
    std::atomic<uint64_t> uploadedThisFrameBytes{0};
    std::atomic<uint32_t> evictedCount{0};
    std::atomic<uint32_t> failedAssets{0};
    std::atomic<int> gpuResidencyValidated{0};
    std::atomic<int> directStorageApiPresent{0};

    std::atomic<float> dynamicLookaheadMeters{100.0f};
    std::atomic<float> dynamicPriorityMultiplier{1.0f};
    std::atomic<float> proactiveLodBias{0.0f};
    std::atomic<uint32_t> neuralInferenceLatencyUs{0};
    std::atomic<int> neuralAccelerated{0};
};

std::mutex g_contextMutex;
std::shared_ptr<StreamingContext> g_context;

// Serializes init/shutdown (including worker joins) without blocking normal
// API calls, which only hold g_contextMutex for the shared_ptr copy.
std::mutex g_lifecycleMutex;

std::shared_ptr<StreamingContext> GetContext() {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    return g_context;
}

uint64_t AlignUp(uint64_t value) {
    return (value + kVramAlignment - 1ull) & ~(kVramAlignment - 1ull);
}

bool VramAllocate(StreamingContext& ctx, uint64_t size, uint64_t* outOffset) {
    if (outOffset == nullptr) {
        return false;
    }
    if (size == 0) {
        *outOffset = 0;
        return true;
    }
    const uint64_t required = AlignUp(size);
    for (size_t index = 0; index < ctx.vramFree.size(); ++index) {
        VramRange& range = ctx.vramFree[index];
        if (range.size < required) {
            continue;
        }
        *outOffset = range.offset;
        if (range.size == required) {
            ctx.vramFree.erase(ctx.vramFree.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            range.offset += required;
            range.size -= required;
        }
        return true;
    }
    return false;
}

void VramFreeRange(StreamingContext& ctx, uint64_t offset, uint64_t size) {
    if (size == 0) {
        return;
    }
    const uint64_t aligned = AlignUp(size);
    ctx.vramFree.push_back(VramRange{offset, aligned});
    std::sort(ctx.vramFree.begin(), ctx.vramFree.end(),
              [](const VramRange& left, const VramRange& right) {
                  return left.offset < right.offset;
              });

    size_t write = 0;
    for (size_t read = 0; read < ctx.vramFree.size(); ++read) {
        const VramRange current = ctx.vramFree[read];
        if (write > 0 && ctx.vramFree[write - 1].offset + ctx.vramFree[write - 1].size ==
                             current.offset) {
            ctx.vramFree[write - 1].size += current.size;
        } else {
            ctx.vramFree[write] = current;
            ++write;
        }
    }
    ctx.vramFree.resize(write);
}

void LruPushFront(StreamingContext& ctx, InternalAsset& asset) {
    asset.lruPrev = nullptr;
    asset.lruNext = ctx.lruHead;
    if (ctx.lruHead != nullptr) {
        ctx.lruHead->lruPrev = &asset;
    } else {
        ctx.lruTail = &asset;
    }
    ctx.lruHead = &asset;
}

void LruRemove(StreamingContext& ctx, InternalAsset& asset) {
    if (asset.lruPrev != nullptr) {
        asset.lruPrev->lruNext = asset.lruNext;
    } else {
        ctx.lruHead = asset.lruNext;
    }
    if (asset.lruNext != nullptr) {
        asset.lruNext->lruPrev = asset.lruPrev;
    } else {
        ctx.lruTail = asset.lruPrev;
    }
    asset.lruPrev = nullptr;
    asset.lruNext = nullptr;
}

void LruTouch(StreamingContext& ctx, InternalAsset& asset) {
    if (ctx.lruHead == &asset) {
        return;
    }
    LruRemove(ctx, asset);
    LruPushFront(ctx, asset);
}

void QueuePush(StreamingContext& ctx, InternalAsset& asset, uint32_t priority) {
    asset.requestPriority = priority;
    asset.state = AssetState::Queued;
    asset.queuePrev = ctx.queueTail[priority];
    asset.queueNext = nullptr;
    if (ctx.queueTail[priority] != nullptr) {
        ctx.queueTail[priority]->queueNext = &asset;
    } else {
        ctx.queueHead[priority] = &asset;
    }
    ctx.queueTail[priority] = &asset;
    ctx.queueCount[priority]++;
    ctx.queuedTotal++;
    ctx.pendingQueueBytes.fetch_add(asset.desc.sizeBytes);
}

void QueueUnlink(StreamingContext& ctx, InternalAsset& asset) {
    const uint32_t priority = asset.requestPriority;
    if (asset.queuePrev != nullptr) {
        asset.queuePrev->queueNext = asset.queueNext;
    } else {
        ctx.queueHead[priority] = asset.queueNext;
    }
    if (asset.queueNext != nullptr) {
        asset.queueNext->queuePrev = asset.queuePrev;
    } else {
        ctx.queueTail[priority] = asset.queuePrev;
    }
    asset.queuePrev = nullptr;
    asset.queueNext = nullptr;
    if (ctx.queueCount[priority] > 0) {
        ctx.queueCount[priority]--;
    }
    if (ctx.queuedTotal > 0) {
        ctx.queuedTotal--;
    }
    ctx.pendingQueueBytes.fetch_sub(asset.desc.sizeBytes);
}

void StagedPush(StreamingContext& ctx, InternalAsset& asset) {
    const uint32_t priority = asset.requestPriority;
    asset.state = AssetState::Staged;
    asset.stagedPrev = ctx.stagedTail[priority];
    asset.stagedNext = nullptr;
    if (ctx.stagedTail[priority] != nullptr) {
        ctx.stagedTail[priority]->stagedNext = &asset;
    } else {
        ctx.stagedHead[priority] = &asset;
    }
    ctx.stagedTail[priority] = &asset;
    ctx.stagedCount[priority]++;
    ctx.stagedTotal++;
    ctx.stagedBytes.fetch_add(asset.desc.sizeBytes);
}

void StagedUnlink(StreamingContext& ctx, InternalAsset& asset) {
    const uint32_t priority = asset.requestPriority;
    if (asset.stagedPrev != nullptr) {
        asset.stagedPrev->stagedNext = asset.stagedNext;
    } else {
        ctx.stagedHead[priority] = asset.stagedNext;
    }
    if (asset.stagedNext != nullptr) {
        asset.stagedNext->stagedPrev = asset.stagedPrev;
    } else {
        ctx.stagedTail[priority] = asset.stagedPrev;
    }
    asset.stagedPrev = nullptr;
    asset.stagedNext = nullptr;
    if (ctx.stagedCount[priority] > 0) {
        ctx.stagedCount[priority]--;
    }
    if (ctx.stagedTotal > 0) {
        ctx.stagedTotal--;
    }
}

void MarkResident(StreamingContext& ctx, InternalAsset& asset) {
    asset.state = AssetState::Resident;
    asset.desc.residentBytes = asset.desc.sizeBytes;
    ctx.residentCount++;
    ctx.currentAllocatedBytes.fetch_add(asset.desc.sizeBytes);
    LruPushFront(ctx, asset);
}

int ProbeDirectStorage() {
#ifdef _WIN32
    // Presence of the DLL plus its documented entry point means the API can
    // be queried. It does not claim that this GPU/driver supports it; the
    // runtime still has to create the DirectStorage factory and queue.
    HMODULE module = LoadLibraryW(L"dstorage.dll");
    if (module == nullptr) {
        return 0;
    }
    const bool hasEntryPoint = GetProcAddress(module, "DStorageGetFactory") != nullptr;
    FreeLibrary(module);
    return hasEntryPoint ? 1 : 0;
#else
    return 0;
#endif
}

void WorkerLoop(std::shared_ptr<StreamingContext> ctx) {
    for (;;) {
        InternalAsset* job = nullptr;
        SpmAssetReadCallback read = nullptr;
        void* userData = nullptr;
        uint64_t assetId = 0;
        uint32_t assetSize = 0;
        std::vector<uint8_t> buffer;

        {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            ctx->queueCv.wait(lock, [&ctx] {
                if (!ctx->running.load(std::memory_order_acquire)) {
                    return true;
                }
                if (ctx->readCallback == nullptr) {
                    return false;
                }
                for (uint32_t priority = 0; priority < SPM_PRIORITY_COUNT; ++priority) {
                    if (ctx->queueHead[priority] != nullptr) {
                        return true;
                    }
                }
                return false;
            });

            if (!ctx->running.load(std::memory_order_acquire)) {
                break;
            }

            for (uint32_t priority = 0; priority < SPM_PRIORITY_COUNT; ++priority) {
                if (ctx->queueHead[priority] != nullptr) {
                    job = ctx->queueHead[priority];
                    break;
                }
            }
            if (job == nullptr) {
                continue;
            }

            QueueUnlink(*ctx, *job);
            job->state = AssetState::Reading;
            assetId = job->desc.assetId;
            assetSize = job->desc.sizeBytes;
            read = ctx->readCallback;
            userData = ctx->callbackUserData;
            buffer = std::move(job->staging);
        }

        // I/O outside the lock: other threads keep querying stats, queueing
        // assets and enforcing the budget while the disk works.
        bool failed = false;
        bool cancelled = false;
        if (assetSize > 0) {
            if (buffer.size() < assetSize) {
                buffer.resize(assetSize);
            }
            uint64_t offset = 0;
            while (offset < assetSize) {
                if (!ctx->running.load(std::memory_order_acquire)) {
                    cancelled = true;
                    break;
                }
                const uint32_t remaining = assetSize - static_cast<uint32_t>(offset);
                const uint32_t chunk = std::min(kReadChunkBytes, remaining);
                int32_t copied = read(userData, assetId, offset,
                                      buffer.data() + static_cast<size_t>(offset), chunk);
                if (copied <= 0 || static_cast<uint32_t>(copied) > chunk) {
                    failed = true;
                    break;
                }
                offset += static_cast<uint32_t>(copied);
            }
        }

        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            job->staging = std::move(buffer);
            if (cancelled || !ctx->running.load(std::memory_order_acquire)) {
                job->state = AssetState::Registered;
                job->uploadedBytes = 0;
                break;
            }
            if (failed) {
                job->state = AssetState::Failed;
                job->desc.residentBytes = 0;
                job->uploadedBytes = 0;
                ctx->failedAssets.fetch_add(1);
            } else {
                job->uploadedBytes = 0;
                StagedPush(*ctx, *job);
            }
        }
    }

    ctx->activeWorkers.fetch_sub(1);
}

} // namespace

int SPM_StreamingInit(uint64_t vramBudgetMb, uint32_t maxUploadMbPerFrame,
                      uint32_t workerCount) {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        if (g_context) {
            return 1; // already initialized
        }
    }

    std::shared_ptr<StreamingContext> ctx;
    try {
        ctx = std::make_shared<StreamingContext>();

        const uint64_t budgetMb = (vramBudgetMb == 0) ? 1720 : vramBudgetMb;
        ctx->totalBudgetBytes = budgetMb * 1024ull * 1024ull;

        const uint32_t uploadMb =
            std::clamp<uint32_t>((maxUploadMbPerFrame == 0) ? 3 : maxUploadMbPerFrame, 1, 512);
        ctx->maxUploadPerFrameBytes = uploadMb * 1024u * 1024u;
        ctx->configuredWorkers = std::clamp<uint32_t>((workerCount == 0) ? 2 : workerCount, 1,
                                                      kMaxWorkers);

        // Eviction starts at 88% and aims at 75%, matching the launcher presets.
        ctx->criticalWatermark = (ctx->totalBudgetBytes * 88ull) / 100ull;
        ctx->targetWatermark = (ctx->totalBudgetBytes * 75ull) / 100ull;

        const uint64_t usable = ctx->totalBudgetBytes & ~(kVramAlignment - 1ull);
        ctx->totalBudgetBytes = usable;
        ctx->backingBudgetBytes = usable;
        if (usable > 0) {
            // Enough slots for the fragmentation a normal frame produces; the
            // vector can still grow, but the eviction path stops allocating.
            ctx->vramFree.reserve(256);
            ctx->vramFree.push_back(VramRange{0, usable});
        }
    } catch (...) {
        return 0;
    }

    ctx->directStorageApiPresent.store(ProbeDirectStorage());
    ctx->running.store(true, std::memory_order_release);

    try {
        for (uint32_t index = 0; index < ctx->configuredWorkers; ++index) {
            ctx->workers[index] = std::thread(WorkerLoop, ctx);
        }
    } catch (...) {
        ctx->running.store(false, std::memory_order_release);
        ctx->queueCv.notify_all();
        for (uint32_t index = 0; index < ctx->configuredWorkers; ++index) {
            if (ctx->workers[index].joinable()) {
                ctx->workers[index].join();
            }
        }
        return 0;
    }

    // The counter is published from here, not by each worker on entry, so a
    // caller that queries stats right after Init observes the real pool size
    // instead of racing the thread scheduler. Workers only decrement on exit.
    ctx->activeWorkers.store(ctx->configuredWorkers, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        g_context = ctx;
    }
    return 1;
}

void SPM_StreamingShutdown(void) {
    std::shared_ptr<StreamingContext> ctx;
    {
        std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
        {
            std::lock_guard<std::mutex> lock(g_contextMutex);
            ctx = std::move(g_context);
            g_context.reset();
        }
        if (!ctx) {
            return;
        }
        ctx->running.store(false, std::memory_order_release);
        ctx->queueCv.notify_all();
        for (std::thread& worker : ctx->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    // ctx is released here; in-flight callers still hold their own reference.
}

int SPM_StreamingSetIoCallbacks(SpmAssetReadCallback readCallback,
                                SpmAssetUploadCallback uploadCallback,
                                SpmAssetReleaseCallback releaseCallback, void* userData) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx || !ctx->running.load(std::memory_order_acquire)) {
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->readCallback = readCallback;
        ctx->uploadCallback = uploadCallback;
        ctx->releaseCallback = releaseCallback;
        ctx->callbackUserData = userData;
    }
    ctx->queueCv.notify_all();
    return 1;
}

int SPM_StreamingGetIntegrationStatus(SpmStreamingIntegration* outStatus) {
    if (outStatus == nullptr) {
        return 0;
    }
    std::memset(outStatus, 0, sizeof(*outStatus));

    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);
    outStatus->ioConnected = ctx->readCallback != nullptr ? 1 : 0;
    outStatus->uploadConnected = ctx->uploadCallback != nullptr ? 1 : 0;
    outStatus->gpuResidencyValidated =
        ctx->gpuResidencyValidated.load(std::memory_order_relaxed);
    outStatus->releaseConnected = ctx->releaseCallback != nullptr ? 1 : 0;
    outStatus->stagedBytes = ctx->stagedBytes.load(std::memory_order_relaxed);
    outStatus->stagedAssets = ctx->stagedTotal;
    outStatus->failedAssets = ctx->failedAssets.load(std::memory_order_relaxed);
    outStatus->residentAssets = ctx->residentCount;
    return 1;
}

int SPM_StreamingSetAssetPinned(uint64_t assetId, int pinned) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(ctx->mutex);
    const auto it = ctx->assets.find(assetId);
    if (it == ctx->assets.end()) {
        return 0;
    }
    it->second.pinned = pinned != 0;
    return 1;
}

int SPM_StreamingSetVisibility(uint64_t assetId, SpmVisibilityState visibility,
                               uint64_t frameIndex, float priorityWeight) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(ctx->mutex);
    if (frameIndex > ctx->currentFrameIndex) {
        ctx->currentFrameIndex = frameIndex;
    }
    const auto it = ctx->assets.find(assetId);
    if (it == ctx->assets.end()) {
        return 0;
    }
    InternalAsset& asset = it->second;
    asset.visibilityState = visibility;
    asset.hasExplicitVisibility = true;
    asset.desc.isVisible = (visibility == SPM_VISIBILITY_VISIBLE) ? 1 : 0;
    asset.priorityWeight = priorityWeight > 0.0f ? priorityWeight : 1.0f;
    if (visibility != SPM_VISIBILITY_INACTIVE) {
        if (frameIndex > asset.lastVisibleFrame) {
            asset.lastVisibleFrame = frameIndex;
        }
        if (frameIndex > asset.desc.lastFrameUsed) {
            asset.desc.lastFrameUsed = frameIndex;
        }
        if (visibility == SPM_VISIBILITY_VISIBLE && asset.state == AssetState::Resident) {
            LruTouch(*ctx, asset);
        }
    }
    return 1;
}

void SPM_StreamingSetHysteresisFrames(uint64_t frames) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return;
    }
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->hysteresisFrames = frames;
}

void SPM_StreamingUpdateGpuBudget(uint64_t totalBudgetMb, uint64_t osAvailableBudgetMb,
                                  int isIntegratedGpu) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return;
    }
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->isIntegratedGpu = (isIntegratedGpu != 0);
    ctx->osAvailableBudgetBytes = osAvailableBudgetMb * 1024ull * 1024ull;

    uint64_t desiredBytes =
        totalBudgetMb > 0 ? (totalBudgetMb * 1024ull * 1024ull) : ctx->backingBudgetBytes;
    if (ctx->osAvailableBudgetBytes > 0) {
        // Integrated GPUs share system RAM with the CPU and OS, so retain a 15% safety margin.
        const uint64_t cappedOsBytes =
            ctx->isIntegratedGpu ? (ctx->osAvailableBudgetBytes * 85ull) / 100ull
                                 : ctx->osAvailableBudgetBytes;
        desiredBytes = (desiredBytes > 0) ? std::min(desiredBytes, cappedOsBytes) : cappedOsBytes;
    }
    if (ctx->backingBudgetBytes > 0 && desiredBytes > ctx->backingBudgetBytes) {
        desiredBytes = ctx->backingBudgetBytes;
    }
    desiredBytes &= ~(kVramAlignment - 1ull);
    ctx->totalBudgetBytes = desiredBytes;
    const uint64_t critPct = ctx->isIntegratedGpu ? 80ull : 88ull;
    const uint64_t targetPct = ctx->isIntegratedGpu ? 68ull : 75ull;
    ctx->criticalWatermark = (ctx->totalBudgetBytes * critPct) / 100ull;
    ctx->targetWatermark = (ctx->totalBudgetBytes * targetPct) / 100ull;
}

void SPM_StreamingSetGpuFence(uint64_t submittedFence, uint64_t completedFence) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return;
    }
    std::lock_guard<std::mutex> lock(ctx->mutex);
    if (submittedFence > ctx->submittedGpuFence) {
        ctx->submittedGpuFence = submittedFence;
    }
    if (completedFence > ctx->completedGpuFence) {
        ctx->completedGpuFence = completedFence;
    }
}

int SPM_StreamingSetAssetFence(uint64_t assetId, uint64_t gpuFenceValue) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(ctx->mutex);
    const auto it = ctx->assets.find(assetId);
    if (it == ctx->assets.end()) {
        return 0;
    }
    if (gpuFenceValue > it->second.gpuFenceValue) {
        it->second.gpuFenceValue = gpuFenceValue;
    }
    return 1;
}

void SPM_StreamingUpdateCamera(const SpmCameraState* camera) {
    if (camera == nullptr) {
        return;
    }
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return;
    }

    uint32_t queued = 0;
    uint64_t allocated = 0;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->camera = *camera;
        ctx->hasCamera = true;
        queued = ctx->queuedTotal;
        allocated = ctx->currentAllocatedBytes.load(std::memory_order_relaxed);
    }

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
    input.currentVramUsedMb =
        static_cast<uint32_t>(allocated / (1024ull * 1024ull));
    input.currentVramBudgetMb =
        static_cast<uint32_t>(ctx->totalBudgetBytes / (1024ull * 1024ull));
    input.pendingLoadQueueSize = queued;

    SpmPredictorOutput output{};
    if (SPM_NeuralAssistPredict(&input, &output) != 0) {
        ctx->dynamicLookaheadMeters.store(output.recommendedLookaheadDistance,
                                          std::memory_order_relaxed);
        ctx->dynamicPriorityMultiplier.store(output.prefetchPriorityMultiplier,
                                             std::memory_order_relaxed);
        ctx->proactiveLodBias.store(output.proactiveLodBias, std::memory_order_relaxed);
        ctx->neuralInferenceLatencyUs.store(output.inferenceTimeMicroseconds,
                                            std::memory_order_relaxed);
        ctx->neuralAccelerated.store(SPM_NeuralAssistIsAccelerated(),
                                     std::memory_order_relaxed);
    }
}

int SPM_StreamingRegisterAsset(const SpmAssetDescriptor* desc) {
    if (desc == nullptr) {
        return 0;
    }
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);
    auto it = ctx->assets.find(desc->assetId);
    if (it == ctx->assets.end()) {
        try {
            InternalAsset asset;
            asset.desc = *desc;
            asset.desc.residentBytes = 0;
            asset.visibilityState =
                desc->isVisible != 0 ? SPM_VISIBILITY_VISIBLE : SPM_VISIBILITY_INACTIVE;
            asset.lastVisibleFrame = desc->lastFrameUsed;
            ctx->assets.emplace(desc->assetId, std::move(asset));
        } catch (...) {
            return 0;
        }
        return 1;
    }

    // The map node (and therefore every intrusive link) stays valid; only the
    // public descriptor is refreshed. Size changes are refused while the
    // asset owns staged/resident bytes because they would break accounting.
    InternalAsset& asset = it->second;
    asset.desc.type = desc->type;
    asset.desc.boundsMin = desc->boundsMin;
    asset.desc.boundsMax = desc->boundsMax;
    asset.desc.mipLevel = desc->mipLevel;
    asset.desc.isVisible = desc->isVisible;
    asset.desc.lastFrameUsed = desc->lastFrameUsed;
    if (!asset.hasExplicitVisibility) {
        asset.visibilityState =
            desc->isVisible != 0 ? SPM_VISIBILITY_VISIBLE : SPM_VISIBILITY_INACTIVE;
    }
    if (desc->lastFrameUsed > asset.lastVisibleFrame) {
        asset.lastVisibleFrame = desc->lastFrameUsed;
    }

    const bool canResize = asset.state == AssetState::Registered ||
                           asset.state == AssetState::Failed;
    if (canResize) {
        asset.desc.sizeBytes = desc->sizeBytes;
        asset.desc.residentBytes = 0;
        asset.uploadedBytes = 0;
        if (asset.state == AssetState::Failed) {
            asset.state = AssetState::Registered;
        }
    }
    return 1;
}

SpmStreamPriority SPM_StreamingEvaluatePriority(const SpmAssetDescriptor* asset) {
    if (asset == nullptr) {
        return SPM_PRIORITY_BACKGROUND;
    }
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return SPM_PRIORITY_BACKGROUND;
    }

    SpmCameraState camera{};
    bool hasCamera = false;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        camera = ctx->camera;
        hasCamera = ctx->hasCamera;
    }
    if (!hasCamera) {
        return asset->isVisible != 0 ? SPM_PRIORITY_CRITICAL : SPM_PRIORITY_BACKGROUND;
    }

    const float centerX = (asset->boundsMin.x + asset->boundsMax.x) * 0.5f;
    const float centerY = (asset->boundsMin.y + asset->boundsMax.y) * 0.5f;
    const float centerZ = (asset->boundsMin.z + asset->boundsMax.z) * 0.5f;

    const float toX = centerX - camera.position.x;
    const float toY = centerY - camera.position.y;
    const float toZ = centerZ - camera.position.z;
    float distance = std::sqrt(toX * toX + toY * toY + toZ * toZ);
    if (distance < 0.001f) {
        distance = 0.001f;
    }
    const float dirX = toX / distance;
    const float dirY = toY / distance;
    const float dirZ = toZ / distance;

    const float forwardDot =
        dirX * camera.forward.x + dirY * camera.forward.y + dirZ * camera.forward.z;

    const float speed = std::sqrt(camera.velocity.x * camera.velocity.x +
                                  camera.velocity.y * camera.velocity.y +
                                  camera.velocity.z * camera.velocity.z);
    float velocityDot = 0.0f;
    if (speed > 0.5f) {
        velocityDot = (dirX * camera.velocity.x + dirY * camera.velocity.y +
                       dirZ * camera.velocity.z) /
                      speed;
    }

    if (asset->isVisible != 0 || (distance <= 25.0f && forwardDot > 0.45f)) {
        return SPM_PRIORITY_CRITICAL;
    }

    const float lookahead = ctx->dynamicLookaheadMeters.load(std::memory_order_relaxed);
    if (speed > 1.0f && velocityDot > 0.60f &&
        distance <= (speed * 2.5f + (lookahead * 0.35f))) {
        return SPM_PRIORITY_PREDICTED;
    }

    if (distance <= lookahead && forwardDot > 0.20f) {
        return SPM_PRIORITY_NORMAL;
    }

    return SPM_PRIORITY_BACKGROUND;
}

int SPM_StreamingQueueRequest(uint64_t assetId, SpmStreamPriority priority) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx || !ctx->running.load(std::memory_order_acquire)) {
        return 0;
    }

    uint32_t bucket = static_cast<uint32_t>(priority);
    if (bucket >= static_cast<uint32_t>(SPM_PRIORITY_COUNT)) {
        bucket = SPM_PRIORITY_NORMAL;
    }

    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        auto it = ctx->assets.find(assetId);
        if (it == ctx->assets.end()) {
            return 0;
        }

        InternalAsset& asset = it->second;
        switch (asset.state) {
        case AssetState::Queued:
        case AssetState::Reading:
        case AssetState::Staged:
            return 1;
        case AssetState::Resident:
            LruTouch(*ctx, asset);
            return 1;
        case AssetState::Failed:
            asset.state = AssetState::Registered;
            asset.uploadedBytes = 0;
            break;
        case AssetState::Registered:
            break;
        }

        if (asset.desc.sizeBytes == 0) {
            // Nothing to read or upload: a zero-byte asset is resident as soon
            // as it is requested.
            MarkResident(*ctx, asset);
            return 1;
        }

        asset.desc.residentBytes = 0;
        asset.uploadedBytes = 0;
        QueuePush(*ctx, asset, bucket);
        notify = true;
    }

    if (notify) {
        ctx->queueCv.notify_one();
    }
    return 1;
}

uint32_t SPM_StreamingProcessTimeSlice(uint32_t maxBytesThisFrame) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx || !ctx->running.load(std::memory_order_acquire)) {
        return 0;
    }

    std::lock_guard<std::mutex> uploader(ctx->uploaderMutex);
    std::lock_guard<std::mutex> lock(ctx->mutex);

    uint32_t quota = (maxBytesThisFrame == 0) ? ctx->maxUploadPerFrameBytes
                                              : maxBytesThisFrame;
    if (quota == 0) {
        quota = ctx->maxUploadPerFrameBytes;
    }
    if (quota == 0 || ctx->uploadCallback == nullptr) {
        // No renderer connected: leave the bytes staged and report zero real
        // upload instead of advancing a counter that does not mean anything.
        ctx->uploadedThisFrameBytes.store(0, std::memory_order_relaxed);
        return 0;
    }

    SpmAssetUploadCallback upload = ctx->uploadCallback;
    void* userData = ctx->callbackUserData;

    uint32_t uploaded = 0;
    for (uint32_t priority = 0; priority < SPM_PRIORITY_COUNT && uploaded < quota;
         ++priority) {
        InternalAsset* node = ctx->stagedHead[priority];
        while (node != nullptr && uploaded < quota) {
            InternalAsset* next = node->stagedNext;
            InternalAsset& asset = *node;

            const uint32_t remaining = asset.desc.sizeBytes - asset.uploadedBytes;
            if (remaining == 0) {
                StagedUnlink(*ctx, asset);
                MarkResident(*ctx, asset);
                ctx->gpuResidencyValidated.store(1, std::memory_order_release);
                node = next;
                continue;
            }

            if (!asset.hasVram) {
                uint64_t offset = 0;
                if (!VramAllocate(*ctx, asset.desc.sizeBytes, &offset)) {
                    // The VRAM budget is exhausted; the asset stays staged and
                    // the next SPM_StreamingEnforceBudget call can make room.
                    node = next;
                    continue;
                }
                asset.vramOffset = offset;
                asset.hasVram = true;
            }

            const uint32_t slice = std::min(remaining, quota - uploaded);
            int32_t copied = upload(userData, asset.desc.assetId,
                                    asset.staging.data() + asset.uploadedBytes, slice,
                                    asset.vramOffset + asset.uploadedBytes);
            if (copied <= 0 || static_cast<uint32_t>(copied) > slice) {
                StagedUnlink(*ctx, asset);
                ctx->stagedBytes.fetch_sub(
                    asset.desc.sizeBytes - asset.uploadedBytes,
                    std::memory_order_relaxed);
                if (asset.hasVram) {
                    VramFreeRange(*ctx, asset.vramOffset, asset.desc.sizeBytes);
                    asset.hasVram = false;
                    asset.vramOffset = 0;
                }
                asset.uploadedBytes = 0;
                asset.desc.residentBytes = 0;
                asset.state = AssetState::Failed;
                ctx->failedAssets.fetch_add(1);
                node = next;
                continue;
            }

            const uint32_t accepted = static_cast<uint32_t>(copied);
            asset.uploadedBytes += accepted;
            uploaded += accepted;
            ctx->stagedBytes.fetch_sub(accepted, std::memory_order_relaxed);

            if (asset.uploadedBytes >= asset.desc.sizeBytes) {
                StagedUnlink(*ctx, asset);
                MarkResident(*ctx, asset);
                // Release the staging capacity: keeping it would retain the
                // full asset size in RAM for every asset ever streamed.
                std::vector<uint8_t>().swap(asset.staging);
                ctx->gpuResidencyValidated.store(1, std::memory_order_release);
            }
            node = next;
        }
    }

    ctx->uploadedThisFrameBytes.store(uploaded, std::memory_order_relaxed);
    return uploaded;
}

int SPM_StreamingEnforceBudget(void) {
    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx || !ctx->running.load(std::memory_order_acquire)) {
        return 0;
    }

    int evicted = 0;
    {
        // The release callback runs under the same lock that serializes VRAM
        // allocation in SPM_StreamingProcessTimeSlice: the renderer must not
        // be told a range is free after another asset has already reused it.
        std::lock_guard<std::mutex> lock(ctx->mutex);
        if (ctx->totalBudgetBytes == 0) {
            return 0; // unlimited
        }

        uint64_t current = ctx->currentAllocatedBytes.load(std::memory_order_relaxed);
        if (current < ctx->criticalWatermark) {
            return 0;
        }

        SpmAssetReleaseCallback release = ctx->releaseCallback;
        void* userData = ctx->callbackUserData;

        const auto isHysteresisExpired = [&](const InternalAsset& asset) -> bool {
            if (ctx->currentFrameIndex == 0 || asset.lastVisibleFrame == 0 ||
                ctx->hysteresisFrames == 0) {
                return true;
            }
            if (ctx->currentFrameIndex <= asset.lastVisibleFrame) {
                return false;
            }
            return (ctx->currentFrameIndex - asset.lastVisibleFrame) >= ctx->hysteresisFrames;
        };

        const auto evictPass = [&](uint64_t goalBytes, auto&& predicate) {
            InternalAsset* node = ctx->lruTail;
            while (node != nullptr && current > goalBytes) {
                InternalAsset* previous = node->lruPrev;
                const bool inFlightGpu = node->gpuFenceValue > ctx->completedGpuFence;
                const bool protectedVisible =
                    node->desc.isVisible != 0 ||
                    node->visibilityState == SPM_VISIBILITY_VISIBLE;
                if (node->state == AssetState::Resident && !node->pinned && !protectedVisible &&
                    !inFlightGpu && predicate(*node)) {
                    const uint64_t freed = node->desc.residentBytes;
                    const uint64_t offset = node->vramOffset;
                    const uint64_t size = node->desc.sizeBytes;
                    const bool hadVram = node->hasVram;

                    if (hadVram && release != nullptr) {
                        release(userData, node->desc.assetId, offset);
                    }

                    LruRemove(*ctx, *node);
                    node->state = AssetState::Registered;
                    node->desc.residentBytes = 0;
                    node->uploadedBytes = 0;
                    node->hasVram = false;
                    node->vramOffset = 0;
                    if (ctx->residentCount > 0) {
                        ctx->residentCount--;
                    }
                    if (current >= freed) {
                        current -= freed;
                    } else {
                        current = 0;
                    }
                    ctx->currentAllocatedBytes.store(current, std::memory_order_relaxed);
                    if (hadVram) {
                        VramFreeRange(*ctx, offset, size);
                    }
                    ctx->evictedCount.fetch_add(1, std::memory_order_relaxed);
                    ++evicted;
                }
                node = previous;
            }
        };

        // Pass 0: Inactive resources past temporal hysteresis window down to targetWatermark.
        evictPass(ctx->targetWatermark, [&](const InternalAsset& a) {
            return a.visibilityState == SPM_VISIBILITY_INACTIVE && isHysteresisExpired(a);
        });
        // Pass 1: Potentially-visible resources past temporal hysteresis under critical pressure.
        if (current > ctx->criticalWatermark) {
            evictPass(ctx->criticalWatermark, [&](const InternalAsset& a) {
                return a.visibilityState == SPM_VISIBILITY_POTENTIALLY_VISIBLE &&
                       isHysteresisExpired(a);
            });
        }
        // Pass 2: Emergency pressure fallback for inactive resources before evicting potentially visible.
        if (current > ctx->criticalWatermark) {
            evictPass(ctx->criticalWatermark, [&](const InternalAsset& a) {
                return a.visibilityState == SPM_VISIBILITY_INACTIVE;
            });
        }
        // Pass 3: Last-resort OOM protection for potentially visible assets (still never evicts visible/pinned/in-flight).
        if (current > ctx->criticalWatermark) {
            evictPass(ctx->criticalWatermark, [&](const InternalAsset& a) {
                return a.visibilityState == SPM_VISIBILITY_POTENTIALLY_VISIBLE;
            });
        }
    }

    if (evicted > 0) {
        // Space was released; wake workers that may be waiting for it.
        ctx->queueCv.notify_all();
    }
    return evicted;
}

void SPM_StreamingGetStats(SpmStreamingStats* outStats) {
    if (outStats == nullptr) {
        return;
    }
    std::memset(outStats, 0, sizeof(*outStats));

    std::shared_ptr<StreamingContext> ctx = GetContext();
    if (!ctx) {
        return;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);
    outStats->totalBudgetBytes = ctx->totalBudgetBytes;
    outStats->currentAllocatedBytes =
        ctx->currentAllocatedBytes.load(std::memory_order_relaxed);
    outStats->pendingQueueBytes = ctx->pendingQueueBytes.load(std::memory_order_relaxed);
    outStats->uploadedThisFrameBytes =
        ctx->uploadedThisFrameBytes.load(std::memory_order_relaxed);
    outStats->activeWorkers = ctx->activeWorkers.load(std::memory_order_relaxed);
    outStats->queuedRequests = ctx->queuedTotal;
    outStats->evictedCount = ctx->evictedCount.load(std::memory_order_relaxed);
    outStats->directStorageAvailable = ctx->directStorageApiPresent.load(
        std::memory_order_relaxed);
    outStats->dynamicLookaheadMeters =
        ctx->dynamicLookaheadMeters.load(std::memory_order_relaxed);
    outStats->dynamicPriorityMultiplier =
        ctx->dynamicPriorityMultiplier.load(std::memory_order_relaxed);
    outStats->proactiveLodBias = ctx->proactiveLodBias.load(std::memory_order_relaxed);
    outStats->neuralInferenceLatencyUs =
        ctx->neuralInferenceLatencyUs.load(std::memory_order_relaxed);
    outStats->neuralAccelerated = ctx->neuralAccelerated.load(std::memory_order_relaxed);
}
