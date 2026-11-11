// SPDX-License-Identifier: GPL-2.0-or-later
// Test suite for SPM Neural Assist Inference Engine
#include "spm/spm_neural_assist.h"
#include <cassert>
#include <cstdio>
#include <cmath>

int main() {
    printf("=== SPMCore Neural Assist Test ===\n");

    // Null safety
    assert(SPM_NeuralAssistPredict(nullptr, nullptr) == 0);

    // Initialize Auto Mode
    int initRes = SPM_NeuralAssistInit(SPM_NEURAL_MODE_AUTO);
    assert(initRes == 1);

    SpmBackendType activeType = SPM_NeuralAssistGetActiveBackend();
    const char* backendName = SPM_NeuralAssistGetBackendName();
    int isAccel = SPM_NeuralAssistIsAccelerated();

    printf("Active Backend: %d (%s), Accelerated: %s\n",
           (int)activeType, backendName, isAccel ? "Yes" : "No");
    assert(activeType != SPM_BACKEND_NONE);

    // 1. Forward movement test
    SpmPredictorInput forwardInput{};
    forwardInput.playerPosX = 0.0f;
    forwardInput.playerPosY = 0.0f;
    forwardInput.playerPosZ = 0.0f;
    forwardInput.playerVelX = 0.0f;
    forwardInput.playerVelY = 0.0f;
    forwardInput.playerVelZ = 12.0f; // Moving forward fast (12 m/s)
    forwardInput.cameraForwardX = 0.0f;
    forwardInput.cameraForwardY = 0.0f;
    forwardInput.cameraForwardZ = 1.0f; // Looking forward
    forwardInput.currentVramUsedMb = 600;
    forwardInput.currentVramBudgetMb = 820;
    forwardInput.pendingLoadQueueSize = 5;

    SpmPredictorOutput out{};
    int predRes = SPM_NeuralAssistPredict(&forwardInput, &out);
    assert(predRes == 1);

    printf("Forward Run: Lookahead=%.1fm, PriorityMult=%.2fx, PredictedVram=%.1fMB, LodBias=%.2f, Latency=%uus\n",
           out.recommendedLookaheadDistance,
           out.prefetchPriorityMultiplier,
           out.predictedShortTermVramMb,
           out.proactiveLodBias,
           out.inferenceTimeMicroseconds);

    // Assert forward movement expands lookahead horizon beyond baseline 100m
    assert(out.recommendedLookaheadDistance > 100.0f);
    assert(out.prefetchPriorityMultiplier > 1.0f);
    // Strict execution budget: must finish in under 1 ms (1000 us)
    assert(out.inferenceTimeMicroseconds < 1000);

    // 2. High VRAM pressure test (approaching budget cap)
    SpmPredictorInput pressureInput = forwardInput;
    pressureInput.currentVramUsedMb = 780; // ~95% of 820 MB budget
    predRes = SPM_NeuralAssistPredict(&pressureInput, &out);
    assert(predRes == 1);
    printf("Pressure Run: Lookahead=%.1fm, PredictedVram=%.1fMB, LodBias=%.2f\n",
           out.recommendedLookaheadDistance,
           out.predictedShortTermVramMb,
           out.proactiveLodBias);
    assert(out.proactiveLodBias > 0.0f); // Proactive LOD throttling triggered

    // 3. Mode switching test: Disabled
    SPM_NeuralAssistSetMode(SPM_NEURAL_MODE_DISABLED);
    assert(SPM_NeuralAssistGetMode() == SPM_NEURAL_MODE_DISABLED);
    predRes = SPM_NeuralAssistPredict(&forwardInput, &out);
    assert(predRes == 1);
    assert(out.recommendedLookaheadDistance == 100.0f);
    assert(out.prefetchPriorityMultiplier == 1.0f);
    assert(out.activeBackend == SPM_BACKEND_NONE);
    printf("Disabled Mode Verified: Baseline lookahead=%.1fm\n", out.recommendedLookaheadDistance);

    // 4. Mode switching test: Re-enabled
    SPM_NeuralAssistSetMode(SPM_NEURAL_MODE_ENABLED);
    assert(SPM_NeuralAssistGetMode() == SPM_NEURAL_MODE_ENABLED);
    predRes = SPM_NeuralAssistPredict(&forwardInput, &out);
    assert(predRes == 1);
    assert(out.recommendedLookaheadDistance > 100.0f);
    printf("Re-enabled Mode Verified: Lookahead=%.1fm\n", out.recommendedLookaheadDistance);

    SPM_NeuralAssistShutdown();
    printf("=== SPMCore Neural Assist Test PASSED ===\n");
    return 0;
}
