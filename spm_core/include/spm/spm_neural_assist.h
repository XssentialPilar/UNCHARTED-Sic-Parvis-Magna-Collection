// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - SPM Neural Assist & Predictive Inference Engine
#ifndef SPM_NEURAL_ASSIST_H
#define SPM_NEURAL_ASSIST_H

#include "spm_types.h"
#include <stdint.h>

#ifdef _WIN32
  #ifdef SPM_CORE_EXPORTS
    #define SPM_NEURAL_API __declspec(dllexport)
  #else
    #define SPM_NEURAL_API __declspec(dllimport)
  #endif
#else
  #define SPM_NEURAL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SpmNeuralAssistMode {
    SPM_NEURAL_MODE_AUTO = 0,     // Use the best verified backend (currently CPU)
    SPM_NEURAL_MODE_ENABLED = 1,  // Force prediction on
    SPM_NEURAL_MODE_DISABLED = 2  // Neutral streaming values
} SpmNeuralAssistMode;

// ABI note: the DirectML/ONNX values are kept so existing binaries keep
// compiling, but no inference backend for them exists. The module never
// reports them as active: doing so would claim acceleration that is not
// taking place.
typedef enum SpmBackendType {
    SPM_BACKEND_NONE = 0,
    SPM_BACKEND_CPU_CONVENTIONAL = 1,
    SPM_BACKEND_DIRECTML_NPU = 2, // reserved, not implemented
    SPM_BACKEND_ONNX_NPU = 3      // reserved, not implemented
} SpmBackendType;

typedef struct SpmPredictorInput {
    float playerPosX, playerPosY, playerPosZ;
    float playerVelX, playerVelY, playerVelZ;
    float cameraForwardX, cameraForwardY, cameraForwardZ;
    float fovDegrees;
    uint32_t currentVramUsedMb;
    uint32_t currentVramBudgetMb;
    uint32_t pendingLoadQueueSize;
    float recentCacheHitRate;
} SpmPredictorInput;

typedef struct SpmPredictorOutput {
    float recommendedLookaheadDistance; // Dynamic meters (e.g. 75.0m - 180.0m)
    float prefetchPriorityMultiplier;   // 1.0x - 2.5x directional urgency
    float predictedShortTermVramMb;     // Estimated VRAM demand in next 2 sec
    float proactiveLodBias;             // 0.0f (full res) to 1.0f (conservative mip)
    uint32_t inferenceTimeMicroseconds; // Execution latency (< 1000 us budget)
    SpmBackendType activeBackend;
} SpmPredictorOutput;

// Honest description of the backend that is actually running. It exists so
// callers can distinguish "NPU hardware installed" from "accelerated
// inference session running" without guessing from DLL presence.
typedef struct SpmNeuralBackendInfo {
    SpmBackendType backend;
    uint8_t hardwareAccelerated; // 1 only with a verified accelerated session
    uint8_t npuHardwareDetected; // from the OS Compute Accelerator class / accel
    uint8_t inferenceSessionActive;
    char description[160];
} SpmNeuralBackendInfo;

SPM_NEURAL_API int  SPM_NeuralAssistInit(SpmNeuralAssistMode mode);
SPM_NEURAL_API void SPM_NeuralAssistShutdown(void);
SPM_NEURAL_API void SPM_NeuralAssistSetMode(SpmNeuralAssistMode mode);
SPM_NEURAL_API SpmNeuralAssistMode SPM_NeuralAssistGetMode(void);
SPM_NEURAL_API SpmBackendType SPM_NeuralAssistGetActiveBackend(void);
SPM_NEURAL_API const char* SPM_NeuralAssistGetBackendName(void);
SPM_NEURAL_API int  SPM_NeuralAssistPredict(const SpmPredictorInput* input, SpmPredictorOutput* output);
SPM_NEURAL_API int  SPM_NeuralAssistIsAccelerated(void);
SPM_NEURAL_API int  SPM_NeuralAssistGetBackendInfo(SpmNeuralBackendInfo* outInfo);

#ifdef __cplusplus
}
#endif

#endif // SPM_NEURAL_ASSIST_H
