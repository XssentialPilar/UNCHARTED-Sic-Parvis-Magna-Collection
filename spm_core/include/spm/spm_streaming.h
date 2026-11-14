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

#ifdef _WIN32
  #define SPM_CALLBACK __cdecl
#else
  #define SPM_CALLBACK
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

typedef enum SpmVisibilityState {
    SPM_VISIBILITY_INACTIVE            = 0,
    SPM_VISIBILITY_POTENTIALLY_VISIBLE = 1,
    SPM_VISIBILITY_VISIBLE             = 2
} SpmVisibilityState;

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
    // 1 when dstorage.dll exports DStorageGetFactory (the API can be queried).
    // It does not mean the GPU/driver actually supports DirectStorage.
    int directStorageAvailable;
    float dynamicLookaheadMeters;
    float dynamicPriorityMultiplier;
    float proactiveLodBias;
    uint32_t neuralInferenceLatencyUs;
    // Always 0 in this build: there is no DirectML/ONNX inference session.
    int neuralAccelerated;
} SpmStreamingStats;

// ---------------------------------------------------------------------------
// Runtime integration
//
// SPMCore does not own the game's files nor its Vulkan device. A connected
// runtime supplies:
//   * a read callback: fills `destination` with `bytes` of asset data at
//     `offset`; returns the bytes copied, 0 on EOF and a negative value on
//     error. This is the only way asset data enters the streamer.
//   * an upload callback: copies the staged bytes into the renderer's real
//     VRAM allocation. Without it, assets stay staged in RAM and the module
//     reports uploadConnected = 0 / gpuResidencyValidated = 0. No upload is
//     simulated.
//   * a release callback: tells the renderer that the VRAM range previously
//     handed to it by the upload callback is free and may be reused.
//
// Callbacks run on streaming worker threads (read) or on the thread calling
// SPM_StreamingProcessTimeSlice / SPM_StreamingEnforceBudget (upload and
// release). They must not call back into the streaming API.
// ---------------------------------------------------------------------------
typedef int32_t (SPM_CALLBACK *SpmAssetReadCallback)(
    void* userData, uint64_t assetId, uint64_t offset, void* destination, uint32_t bytes);
typedef int32_t (SPM_CALLBACK *SpmAssetUploadCallback)(
    void* userData, uint64_t assetId, const void* source, uint32_t bytes, uint64_t vramOffset);
typedef int32_t (SPM_CALLBACK *SpmAssetReleaseCallback)(
    void* userData, uint64_t assetId, uint64_t vramOffset);

typedef struct SpmStreamingIntegration {
    uint8_t  ioConnected;             // read callback registered
    uint8_t  uploadConnected;         // upload callback registered
    uint8_t  gpuResidencyValidated;   // at least one real upload completed
    uint8_t  releaseConnected;        // release callback registered
    uint64_t stagedBytes;             // asset bytes waiting in RAM for upload
    uint32_t stagedAssets;
    uint32_t failedAssets;
    uint32_t residentAssets;
} SpmStreamingIntegration;

// --- Engine Lifecycle ---
SPM_API int  SPM_StreamingInit(uint64_t vramBudgetMb, uint32_t maxUploadMbPerFrame, uint32_t workerCount);
SPM_API void SPM_StreamingShutdown(void);

// --- Runtime Connection ---
// Must be called after SPM_StreamingInit. Passing null callbacks disconnects
// the corresponding side. Returns 1 on success, 0 if the engine is not running.
SPM_API int  SPM_StreamingSetIoCallbacks(SpmAssetReadCallback readCallback,
                                         SpmAssetUploadCallback uploadCallback,
                                         SpmAssetReleaseCallback releaseCallback,
                                         void* userData);
SPM_API int  SPM_StreamingGetIntegrationStatus(SpmStreamingIntegration* outStatus);

// Pinned assets are never evicted by SPM_StreamingEnforceBudget.
SPM_API int  SPM_StreamingSetAssetPinned(uint64_t assetId, int pinned);

// Three-state visibility & temporal hysteresis updates (never evict Visible or GPU-in-flight).
SPM_API int  SPM_StreamingSetVisibility(uint64_t assetId, SpmVisibilityState visibility,
                                        uint64_t frameIndex, float priorityWeight);
SPM_API void SPM_StreamingSetHysteresisFrames(uint64_t frames);

// Dynamic GPU budget & integrated-GPU shared memory awareness + GPU fence lifetime protection.
SPM_API void SPM_StreamingUpdateGpuBudget(uint64_t totalBudgetMb, uint64_t osAvailableBudgetMb,
                                          int isIntegratedGpu);
SPM_API void SPM_StreamingSetGpuFence(uint64_t submittedFence, uint64_t completedFence);
SPM_API int  SPM_StreamingSetAssetFence(uint64_t assetId, uint64_t gpuFenceValue);

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
