// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Predictive streaming, priority and budget accounting
#include "spm/spm_streaming.h"
#include "spm/spm_neural_assist.h"
#include "test_check.h"

#include <cstdio>
#include <cstring>

namespace {

SpmAssetDescriptor MakeAsset(uint64_t id, SpmResourceType type, float minZ, float maxZ,
                             uint32_t sizeBytes, int visible) {
    SpmAssetDescriptor asset{};
    asset.assetId = id;
    asset.type = type;
    asset.boundsMin = SpmVector3{-2.0f, -2.0f, minZ};
    asset.boundsMax = SpmVector3{2.0f, 2.0f, maxZ};
    asset.sizeBytes = sizeBytes;
    asset.isVisible = visible;
    return asset;
}

} // namespace

int main() {
    std::printf("[TEST] Running test_spm_streaming...\n");

    REQUIRE(SPM_StreamingInit(100, 2, 2) == 1);
    // A second init while running is idempotent and keeps the original budget.
    CHECK(SPM_StreamingInit(1, 1, 1) == 1);

    SpmStreamingStats stats{};
    SPM_StreamingGetStats(&stats);
    CHECK(stats.totalBudgetBytes == 100ull * 1024ull * 1024ull);
    CHECK(stats.activeWorkers == 2);
    CHECK(stats.queuedRequests == 0);
    CHECK(stats.currentAllocatedBytes == 0);

    SPM_NeuralAssistInit(SPM_NEURAL_MODE_AUTO);

    SpmCameraState camera{};
    camera.position = SpmVector3{0.0f, 0.0f, 0.0f};
    camera.forward = SpmVector3{0.0f, 0.0f, 1.0f};
    camera.velocity = SpmVector3{0.0f, 0.0f, 10.0f};
    camera.fovDegrees = 60.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 500.0f;
    SPM_StreamingUpdateCamera(&camera);

    SPM_StreamingGetStats(&stats);
    std::printf("  Lookahead %.1f m, priority x%.2f, inference %u us, accelerated %d\n",
                stats.dynamicLookaheadMeters, stats.dynamicPriorityMultiplier,
                stats.neuralInferenceLatencyUs, stats.neuralAccelerated);
    CHECK(stats.dynamicLookaheadMeters > 100.0f);
    CHECK(stats.dynamicPriorityMultiplier > 1.0f);
    // No accelerated inference session exists, so the flag must stay 0 even on
    // a machine with an NPU.
    CHECK(stats.neuralAccelerated == 0);

    const SpmAssetDescriptor assetA =
        MakeAsset(101, SPM_RES_TEXTURE_MIP, 8.0f, 12.0f, 4u * 1024u * 1024u, 1);
    const SpmAssetDescriptor assetB =
        MakeAsset(102, SPM_RES_GEOMETRY_LOD, 40.0f, 50.0f, 8u * 1024u * 1024u, 0);
    const SpmAssetDescriptor assetC =
        MakeAsset(103, SPM_RES_TEXTURE_MIP, -160.0f, -140.0f, 16u * 1024u * 1024u, 0);

    REQUIRE(SPM_StreamingRegisterAsset(&assetA) == 1);
    REQUIRE(SPM_StreamingRegisterAsset(&assetB) == 1);
    REQUIRE(SPM_StreamingRegisterAsset(&assetC) == 1);

    CHECK(SPM_StreamingEvaluatePriority(&assetA) == SPM_PRIORITY_CRITICAL);
    CHECK(SPM_StreamingEvaluatePriority(&assetB) == SPM_PRIORITY_PREDICTED);
    CHECK(SPM_StreamingEvaluatePriority(&assetC) == SPM_PRIORITY_BACKGROUND);

    REQUIRE(SPM_StreamingQueueRequest(101, SPM_PRIORITY_CRITICAL) == 1);
    REQUIRE(SPM_StreamingQueueRequest(102, SPM_PRIORITY_PREDICTED) == 1);
    REQUIRE(SPM_StreamingQueueRequest(103, SPM_PRIORITY_BACKGROUND) == 1);
    // Requesting an already queued asset is a no-op, not a duplicate.
    CHECK(SPM_StreamingQueueRequest(101, SPM_PRIORITY_CRITICAL) == 1);

    SPM_StreamingGetStats(&stats);
    CHECK(stats.queuedRequests == 3);
    CHECK(stats.pendingQueueBytes == 28ull * 1024ull * 1024ull);

    SpmStreamingIntegration integration{};
    REQUIRE(SPM_StreamingGetIntegrationStatus(&integration) == 1);
    CHECK(integration.ioConnected == 0);
    CHECK(integration.uploadConnected == 0);
    CHECK(integration.releaseConnected == 0);
    CHECK(integration.stagedAssets == 0);
    CHECK(integration.residentAssets == 0);
    CHECK(integration.gpuResidencyValidated == 0);

    // Without a runtime connection nothing can be staged or uploaded: the
    // engine must not advance counters or claim VRAM residency.
    CHECK(SPM_StreamingProcessTimeSlice(2u * 1024u * 1024u) == 0);
    SPM_StreamingGetStats(&stats);
    CHECK(stats.uploadedThisFrameBytes == 0);
    CHECK(stats.queuedRequests == 3);
    CHECK(stats.currentAllocatedBytes == 0);

    CHECK(SPM_StreamingSetAssetPinned(424242, 1) == 0); // unknown asset
    CHECK(SPM_StreamingSetAssetPinned(101, 1) == 1);
    CHECK(SPM_StreamingSetAssetPinned(101, 0) == 1);

    SPM_StreamingShutdown();

    // After shutdown every query is empty and requests are refused instead of
    // touching a destroyed engine.
    SPM_StreamingGetStats(&stats);
    CHECK(stats.totalBudgetBytes == 0);
    CHECK(stats.queuedRequests == 0);
    CHECK(SPM_StreamingQueueRequest(101, SPM_PRIORITY_CRITICAL) == 0);
    CHECK(SPM_StreamingGetIntegrationStatus(&integration) == 0);

    SPM_NeuralAssistShutdown();
    return spm_test::Finish("test_spm_streaming");
}
