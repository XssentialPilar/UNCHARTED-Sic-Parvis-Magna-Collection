// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Neural Assist mode benchmark (AUTO / ENABLED / DISABLED).
//
// This build has exactly one prediction backend: a heuristic CPU predictor.
// There is no DirectML/ONNX/NPU inference session, so the benchmark never
// labels a mode as NPU-accelerated and never fabricates an acceleration
// number. What it does measure is real: the wall-clock cost of
// SPM_StreamingUpdateCamera (the call the runtime would make every frame),
// the values the predictor returns, and the streaming queues those values
// produce.
//
// No read/upload callbacks are connected here, which is exactly why the
// benchmark expects zero staged bytes, zero resident bytes and zero uploads:
// an unconnected engine must not pretend that it moved data to VRAM.
#include "spm/spm_streaming.h"
#include "spm/spm_neural_assist.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

namespace {

constexpr int kFrameCount = 3000;
constexpr uint64_t kAssetCount = 50;
constexpr uint32_t kAssetBytes = 2u * 1024u * 1024u;
constexpr uint64_t kBudgetMb = 100;

struct ModeResult {
    const char* modeName;
    const char* backendName;
    int accelerated;
    double avgUpdateUs;
    double p50UpdateUs;
    double p95UpdateUs;
    double p99UpdateUs;
    double maxUpdateUs;
    double avgReportedInferenceUs;
    double avgLookaheadM;
    double avgPriorityMult;
    int lodTriggers;
    uint32_t queuedRequests;
    uint64_t pendingQueueBytes;
    uint32_t stagedAssets;
    uint32_t residentAssets;
    int gpuResidencyValidated;
};

ModeResult RunMode(SpmNeuralAssistMode mode, const char* name) {
    SPM_NeuralAssistInit(mode);
    SPM_StreamingInit(kBudgetMb, 3, 2);

    for (uint64_t id = 1; id <= kAssetCount; ++id) {
        SpmAssetDescriptor desc{};
        desc.assetId = id;
        desc.type = (id % 3 == 0) ? SPM_RES_GEOMETRY_LOD : SPM_RES_TEXTURE_MIP;
        const float zPos = static_cast<float>(id) * 15.0f;
        desc.boundsMin = SpmVector3{-5.0f, -5.0f, zPos - 5.0f};
        desc.boundsMax = SpmVector3{5.0f, 5.0f, zPos + 5.0f};
        desc.sizeBytes = kAssetBytes;
        desc.isVisible = 0;
        SPM_StreamingRegisterAsset(&desc);
        // A runtime requests assets as the camera moves; these requests stay
        // queued because this benchmark intentionally has no I/O callbacks.
        SPM_StreamingQueueRequest(id, SPM_PRIORITY_NORMAL);
    }

    std::vector<double> updateLatencies;
    updateLatencies.reserve(kFrameCount);

    double sumReportedInferenceUs = 0.0;
    double sumLookahead = 0.0;
    double sumPriorityMult = 0.0;
    int lodTriggers = 0;

    for (int frame = 0; frame < kFrameCount; ++frame) {
        const float seconds = static_cast<float>(frame) / 60.0f;
        const float speed = 8.0f + 5.0f * std::sin(seconds * 0.5f);

        SpmCameraState camera{};
        camera.position = SpmVector3{0.0f, 0.0f, static_cast<float>(frame) * 1.5f};
        camera.forward = SpmVector3{0.0f, 0.0f, 1.0f};
        camera.velocity = SpmVector3{0.0f, 0.0f, speed};
        camera.fovDegrees = 65.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 500.0f;

        const auto start = std::chrono::steady_clock::now();
        SPM_StreamingUpdateCamera(&camera);
        const auto end = std::chrono::steady_clock::now();
        updateLatencies.push_back(
            std::chrono::duration<double, std::micro>(end - start).count());

        SpmStreamingStats stats{};
        SPM_StreamingGetStats(&stats);
        sumReportedInferenceUs += static_cast<double>(stats.neuralInferenceLatencyUs);
        sumLookahead += static_cast<double>(stats.dynamicLookaheadMeters);
        sumPriorityMult += static_cast<double>(stats.dynamicPriorityMultiplier);
        if (stats.proactiveLodBias > 0.0f) {
            ++lodTriggers;
        }

        // Real time-slice contract: without an upload callback it reports zero
        // bytes moved instead of advancing a counter.
        SPM_StreamingProcessTimeSlice(3u * 1024u * 1024u);
        SPM_StreamingEnforceBudget();
    }

    std::vector<double> sorted = updateLatencies;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&sorted](double fraction) {
        const size_t index = static_cast<size_t>(fraction * static_cast<double>(sorted.size()));
        return sorted[std::min(index, sorted.size() - 1)];
    };

    SpmStreamingStats stats{};
    SPM_StreamingGetStats(&stats);
    SpmStreamingIntegration integration{};
    SPM_StreamingGetIntegrationStatus(&integration);

    ModeResult result{};
    result.modeName = name;
    result.backendName = SPM_NeuralAssistGetBackendName();
    result.accelerated = SPM_NeuralAssistIsAccelerated();
    result.avgUpdateUs =
        std::accumulate(updateLatencies.begin(), updateLatencies.end(), 0.0) /
        static_cast<double>(updateLatencies.size());
    result.p50UpdateUs = percentile(0.50);
    result.p95UpdateUs = percentile(0.95);
    result.p99UpdateUs = percentile(0.99);
    result.maxUpdateUs = sorted.back();
    result.avgReportedInferenceUs =
        sumReportedInferenceUs / static_cast<double>(kFrameCount);
    result.avgLookaheadM = sumLookahead / static_cast<double>(kFrameCount);
    result.avgPriorityMult = sumPriorityMult / static_cast<double>(kFrameCount);
    result.lodTriggers = lodTriggers;
    result.queuedRequests = stats.queuedRequests;
    result.pendingQueueBytes = stats.pendingQueueBytes;
    result.stagedAssets = integration.stagedAssets;
    result.residentAssets = integration.residentAssets;
    result.gpuResidencyValidated = integration.gpuResidencyValidated;

    SPM_StreamingShutdown();
    SPM_NeuralAssistShutdown();
    return result;
}

void Print(const ModeResult& result) {
    std::printf("Mode: %s\n", result.modeName);
    std::printf("  Backend: %s\n", result.backendName);
    std::printf("  Hardware accelerated: %s\n", result.accelerated ? "Yes" : "No");
    std::printf("  SPM_StreamingUpdateCamera (us): avg=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f\n",
                result.avgUpdateUs, result.p50UpdateUs, result.p95UpdateUs,
                result.p99UpdateUs, result.maxUpdateUs);
    std::printf("  Predictor-reported inference: %.2f us/call\n",
                result.avgReportedInferenceUs);
    std::printf("  Streaming horizon: lookahead=%.1f m, priority=%.2fx, LOD triggers=%d\n",
                result.avgLookaheadM, result.avgPriorityMult, result.lodTriggers);
    std::printf("  Queue (no I/O backend connected): %u requests / %llu MiB pending\n",
                result.queuedRequests,
                static_cast<unsigned long long>(result.pendingQueueBytes / (1024ull * 1024ull)));
    std::printf("  Real transfers: staged=%u resident=%u gpuResidencyValidated=%d\n\n",
                result.stagedAssets, result.residentAssets,
                result.gpuResidencyValidated);
}

} // namespace

int main() {
    std::printf("=== SPMCore Neural Assist Mode Benchmark ===\n");
    std::printf("Frames per mode: %d\n", kFrameCount);
    std::printf("NPU/DirectML/ONNX backend: NOT IMPLEMENTED in this build\n");
    std::printf("Every mode below runs the heuristic CPU predictor (or is disabled);\n");
    std::printf("none of them is an NPU inference session.\n\n");

    const ModeResult autoResult = RunMode(SPM_NEURAL_MODE_AUTO, "AUTO (CPU predictor)");
    const ModeResult enabledResult =
        RunMode(SPM_NEURAL_MODE_ENABLED, "ENABLED (CPU predictor)");
    const ModeResult disabledResult =
        RunMode(SPM_NEURAL_MODE_DISABLED, "DISABLED (neutral values)");

    Print(autoResult);
    Print(enabledResult);
    Print(disabledResult);

    bool ok = true;

    // 1. The per-frame call must stay far below the 16.666 ms frame budget.
    for (const ModeResult* result : {&autoResult, &enabledResult, &disabledResult}) {
        if (result->p99UpdateUs >= 1000.0) {
            std::printf("[FAIL] %s: p99 update latency %.2f us exceeds 1000 us\n",
                        result->modeName, result->p99UpdateUs);
            ok = false;
        }
    }

    // 2. Honesty gate: no mode may claim hardware acceleration.
    for (const ModeResult* result : {&autoResult, &enabledResult, &disabledResult}) {
        if (result->accelerated != 0) {
            std::printf("[FAIL] %s: claimed hardware acceleration without an NPU session\n",
                        result->modeName);
            ok = false;
        }
    }

    // 3. AUTO and ENABLED must widen the horizon for a moving camera.
    for (const ModeResult* result : {&autoResult, &enabledResult}) {
        if (!(result->avgLookaheadM > 100.0) || !(result->avgPriorityMult > 1.0)) {
            std::printf("[FAIL] %s: predictor returned neutral values while moving\n",
                        result->modeName);
            ok = false;
        }
    }

    // 4. DISABLED must be exactly neutral.
    if (autoResult.avgLookaheadM == disabledResult.avgLookaheadM &&
        autoResult.avgPriorityMult == disabledResult.avgPriorityMult) {
        std::printf("[FAIL] DISABLED is indistinguishable from AUTO\n");
        ok = false;
    }
    if (disabledResult.avgLookaheadM < 99.9 || disabledResult.avgLookaheadM > 100.1 ||
        disabledResult.avgPriorityMult < 0.999 || disabledResult.avgPriorityMult > 1.001 ||
        disabledResult.lodTriggers != 0) {
        std::printf("[FAIL] DISABLED mode did not return neutral streaming values\n");
        ok = false;
    }

    // 5. No I/O backend is connected, so nothing may be staged, resident or
    //    uploaded. This is the check that prevents "fake VRAM".
    for (const ModeResult* result : {&autoResult, &enabledResult, &disabledResult}) {
        if (result->stagedAssets != 0 || result->residentAssets != 0 ||
            result->gpuResidencyValidated != 0 || result->queuedRequests != kAssetCount) {
            std::printf("[FAIL] %s: reported transfers without an I/O backend\n",
                        result->modeName);
            ok = false;
        }
    }

    if (!ok) {
        return 1;
    }
    std::printf("[PASS] Real latency within budget, no fabricated acceleration or transfers.\n");
    return 0;
}
