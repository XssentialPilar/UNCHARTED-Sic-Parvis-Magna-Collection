// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Intelligent Predictive Streaming & Budget Enforcement
#include "spm/spm_streaming.h"
#include "spm/spm_neural_assist.h"
#include <cstdio>
#include <cassert>
#include <thread>
#include <chrono>

int main() {
    printf("[TEST] Running test_spm_streaming...\n");

    // 1. Initialize streaming with 100 MB budget, 2 MB max upload per frame, 2 workers
    int initRes = SPM_StreamingInit(100, 2, 2);
    assert(initRes == 1);
    printf("  SPM_StreamingInit success.\n");

    SpmStreamingStats stats{};
    SPM_StreamingGetStats(&stats);
    assert(stats.totalBudgetBytes == 100ULL * 1024ULL * 1024ULL);
    printf("  Initial budget: %llu bytes, DirectStorage detected: %d\n",
           stats.totalBudgetBytes, stats.directStorageAvailable);

    // 2. Setup Camera at origin looking along +Z with velocity moving +Z
    SPM_NeuralAssistInit(SPM_NEURAL_MODE_AUTO);

    SpmCameraState cam{};
    cam.position = { 0.0f, 0.0f, 0.0f };
    cam.forward  = { 0.0f, 0.0f, 1.0f };
    cam.velocity = { 0.0f, 0.0f, 10.0f }; // moving forward at 10 m/s
    cam.fovDegrees = 60.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 500.0f;
    SPM_StreamingUpdateCamera(&cam);

    SPM_StreamingGetStats(&stats);
    printf("  Dynamic lookahead: %.1f m, Priority mult: %.2f x, Neural lat: %u us, Accel: %d\n",
           stats.dynamicLookaheadMeters, stats.dynamicPriorityMultiplier,
           stats.neuralInferenceLatencyUs, stats.neuralAccelerated);
    assert(stats.dynamicLookaheadMeters > 100.0f);
    assert(stats.dynamicPriorityMultiplier > 1.0f);

    // 3. Test Priority Predictions
    // Asset A: directly in front, close (10m)
    SpmAssetDescriptor assetA{};
    assetA.assetId = 101;
    assetA.type = SPM_RES_TEXTURE_MIP;
    assetA.boundsMin = { -2.0f, -2.0f, 8.0f };
    assetA.boundsMax = {  2.0f,  2.0f, 12.0f };
    assetA.sizeBytes = 4 * 1024 * 1024; // 4 MB
    assetA.isVisible = 1;
    SPM_StreamingRegisterAsset(&assetA);

    SpmStreamPriority pA = SPM_StreamingEvaluatePriority(&assetA);
    printf("  Asset A priority: %d (Expected: %d CRITICAL)\n", pA, SPM_PRIORITY_CRITICAL);
    assert(pA == SPM_PRIORITY_CRITICAL);

    // Asset B: along velocity vector, further ahead (45m)
    SpmAssetDescriptor assetB{};
    assetB.assetId = 102;
    assetB.type = SPM_RES_GEOMETRY_LOD;
    assetB.boundsMin = { -1.0f, -1.0f, 40.0f };
    assetB.boundsMax = {  1.0f,  1.0f, 50.0f };
    assetB.sizeBytes = 8 * 1024 * 1024; // 8 MB
    assetB.isVisible = 0;
    SPM_StreamingRegisterAsset(&assetB);

    SpmStreamPriority pB = SPM_StreamingEvaluatePriority(&assetB);
    printf("  Asset B priority: %d (Expected: %d PREDICTED)\n", pB, SPM_PRIORITY_PREDICTED);
    assert(pB == SPM_PRIORITY_PREDICTED);

    // Asset C: far behind the camera (-150m)
    SpmAssetDescriptor assetC{};
    assetC.assetId = 103;
    assetC.type = SPM_RES_TEXTURE_MIP;
    assetC.boundsMin = { -5.0f, -5.0f, -160.0f };
    assetC.boundsMax = {  5.0f,  5.0f, -140.0f };
    assetC.sizeBytes = 16 * 1024 * 1024; // 16 MB
    assetC.isVisible = 0;
    SPM_StreamingRegisterAsset(&assetC);

    SpmStreamPriority pC = SPM_StreamingEvaluatePriority(&assetC);
    printf("  Asset C priority: %d (Expected: %d BACKGROUND)\n", pC, SPM_PRIORITY_BACKGROUND);
    assert(pC == SPM_PRIORITY_BACKGROUND);

    // 4. Test Queue and Time-Sliced Upload
    SPM_StreamingQueueRequest(101, pA);
    SPM_StreamingQueueRequest(102, pB);

    // Process frame 1 with 2 MB quota
    uint32_t uploaded1 = SPM_StreamingProcessTimeSlice(2 * 1024 * 1024);
    printf("  Frame 1 uploaded: %u bytes (Quota: %u bytes)\n", uploaded1, 2 * 1024 * 1024);
    assert(uploaded1 <= 2 * 1024 * 1024);

    // Process frame 2 with 2 MB quota
    uint32_t uploaded2 = SPM_StreamingProcessTimeSlice(2 * 1024 * 1024);
    printf("  Frame 2 uploaded: %u bytes\n", uploaded2);
    assert(uploaded2 <= 2 * 1024 * 1024);

    // 5. Test Memory Budget Enforcement & LRU Eviction
    // Force allocate large mock assets to cross the 88% watermark of 100 MB
    for (uint64_t id = 200; id < 210; ++id) {
        SpmAssetDescriptor mockDesc{};
        mockDesc.assetId = id;
        mockDesc.type = SPM_RES_TEXTURE_MIP;
        mockDesc.sizeBytes = 10 * 1024 * 1024; // 10 MB each
        mockDesc.residentBytes = 10 * 1024 * 1024;
        mockDesc.isVisible = 0; // not visible, candidate for LRU eviction
        SPM_StreamingRegisterAsset(&mockDesc);
        SPM_StreamingQueueRequest(id, SPM_PRIORITY_BACKGROUND);
    }

    // Process uploads to artificially inflate allocated bytes past 88 MB
    SPM_StreamingProcessTimeSlice(90 * 1024 * 1024);
    SPM_StreamingGetStats(&stats);
    printf("  Allocated before budget enforcement: %llu bytes\n", stats.currentAllocatedBytes);

    int evicted = SPM_StreamingEnforceBudget();
    printf("  Evicted assets to restore memory pressure target: %d\n", evicted);
    assert(evicted > 0);

    SPM_StreamingGetStats(&stats);
    printf("  Allocated after budget enforcement: %llu bytes (Target <= 75MB)\n", stats.currentAllocatedBytes);
    assert(stats.currentAllocatedBytes <= (100ULL * 1024ULL * 1024ULL * 75ULL) / 100ULL);

    // 6. Clean shutdown
    SPM_StreamingShutdown();
    SPM_NeuralAssistShutdown();
    printf("[PASS] test_spm_streaming passed successfully.\n");
    return 0;
}
