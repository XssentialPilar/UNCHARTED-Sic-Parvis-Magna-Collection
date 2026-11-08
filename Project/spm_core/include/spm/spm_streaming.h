// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Intelligent Predictive Asset Streaming & Memory Budgeting Engine
#ifndef SPM_STREAMING_H
#define SPM_STREAMING_H

#include "spm_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #ifdef SPM_CORE_EXPORTS
    #define SPM_API __declspec(dllexport)
  #else
    #define SPM_API __declspec(dllimport)
  #endif
#else
  #define SPM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpmVector3 {
    float x;
    float y;
    float z;
} SpmVector3;

typedef struct SpmCameraState {
    SpmVector3 position;
    SpmVector3 forward;
    SpmVector3 velocity;
    float fovDegrees;
    float nearPlane;
    float farPlane;
} SpmCameraState;

typedef enum SpmStreamPriority {
    SPM_PRIORITY_CRITICAL   = 0, // Immediately visible within camera frustum
    SPM_PRIORITY_PREDICTED  = 1, // Velocity projected forward cone
    SPM_PRIORITY_NORMAL     = 2, // Secondary / mid-range geometry & materials
    SPM_PRIORITY_BACKGROUND = 3, // Distant LODs, background audio, full mip levels
    SPM_PRIORITY_COUNT      = 4
} SpmStreamPriority;

typedef enum SpmResourceType {
    SPM_RES_TEXTURE_MIP  = 0,
    SPM_RES_GEOMETRY_LOD = 1,
    SPM_RES_AUDIO_STREAM = 2,
    SPM_RES_ANIMATION    = 3
} SpmResourceType;

typedef struct SpmAssetDescriptor {
    uint64_t assetId;
    SpmResourceType type;
    SpmVector3 boundsMin;
    SpmVector3 boundsMax;
    uint32_t sizeBytes;
    uint32_t residentBytes;
    uint32_t mipLevel;
    int isVisible;
    uint64_t lastFrameUsed;
} SpmAssetDescriptor;

typedef struct SpmStreamingStats {
    uint64_t totalBudgetBytes;
    uint64_t currentAllocatedBytes;
    uint64_t pendingQueueBytes;
    uint64_t uploadedThisFrameBytes;
    uint32_t activeWorkers;
    uint32_t queuedRequests;
    uint32_t evictedCount;
    int directStorageAvailable;
    float dynamicLookaheadMeters;
    float dynamicPriorityMultiplier;
    float proactiveLodBias;
    uint32_t neuralInferenceLatencyUs;
    int neuralAccelerated;
} SpmStreamingStats;

// --- Engine Lifecycle ---
SPM_API int  SPM_StreamingInit(uint64_t vramBudgetMb, uint32_t maxUploadMbPerFrame, uint32_t workerCount);
SPM_API void SPM_StreamingShutdown(void);

// --- Closed-Loop Predictive Streaming Pipeline ---
// Step 1: Update Player & Camera transforms
SPM_API void SPM_StreamingUpdateCamera(const SpmCameraState* camera);

// Step 2: Register assets in scene database
SPM_API int  SPM_StreamingRegisterAsset(const SpmAssetDescriptor* desc);

// Step 3: Predictive evaluation - tests camera cone and projects velocity lookahead
SPM_API SpmStreamPriority SPM_StreamingEvaluatePriority(const SpmAssetDescriptor* asset);

// Step 4: Queue asset load request into priority buckets
SPM_API int  SPM_StreamingQueueRequest(uint64_t assetId, SpmStreamPriority priority);

// Step 5: Time-sliced gradual upload (called by DeterministicPresent Step 3)
SPM_API uint32_t SPM_StreamingProcessTimeSlice(uint32_t maxBytesThisFrame);

// Step 6: Memory budget enforcement & LRU eviction (called by DeterministicPresent Step 2)
SPM_API int  SPM_StreamingEnforceBudget(void);

// Step 7: Telemetry & diagnostics
SPM_API void SPM_StreamingGetStats(SpmStreamingStats* outStats);

#ifdef __cplusplus
}
#endif

#endif // SPM_STREAMING_H
