// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - SPM Neural Assist & Predictive Inference Implementation
//
// This module implements a deterministic, allocation-free heuristic predictor
// that runs on the CPU. It deliberately does NOT expose a DirectML/ONNX
// backend: loading those DLLs is not an inference session and running the CPU
// heuristic under their name would be a false acceleration claim. If a real
// NPU execution provider is added later, it must create a session, execute it
// on the device and only then report IsHardwareAccelerated() == 1.
#include "spm/spm_neural_assist.h"
#include "spm/spm_hardware_caps.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace {

class SpmCpuPredictor {
public:
    bool Initialize() {
        m_initialized = true;
        return true;
    }

    void Shutdown() { m_initialized = false; }

    bool IsInitialized() const { return m_initialized; }

    bool Predict(const SpmPredictorInput& input, SpmPredictorOutput& output) {
        const auto start = std::chrono::steady_clock::now();

        // 1. Player speed.
        const float speedSq = input.playerVelX * input.playerVelX +
                              input.playerVelY * input.playerVelY +
                              input.playerVelZ * input.playerVelZ;
        const float speed = std::sqrt(speedSq);

        // 2. Alignment between the movement direction and the camera.
        float alignment = 0.0f;
        if (speed > 0.001f) {
            alignment = (input.playerVelX / speed) * input.cameraForwardX +
                        (input.playerVelY / speed) * input.cameraForwardY +
                        (input.playerVelZ / speed) * input.cameraForwardZ;
        }

        // 3. Dynamic lookahead horizon (base 100 m, range 75-180 m).
        float lookahead = 100.0f;
        float priorityMultiplier = 1.0f;
        if (alignment > 0.05f) {
            const float forwardFactor = std::min(alignment, 1.0f);
            const float speedFactor = std::min(speed / 15.0f, 1.0f);
            lookahead = 100.0f + (80.0f * forwardFactor * speedFactor);
            priorityMultiplier = 1.0f + (1.5f * forwardFactor * speedFactor);
        } else if (speed < 0.5f) {
            lookahead = 85.0f;
        } else {
            lookahead = 95.0f;
            priorityMultiplier = 1.1f;
        }

        // 4. Short-term VRAM demand for the next few seconds.
        const float estimatedDelta = (speed * 1.8f) +
                                     static_cast<float>(input.pendingLoadQueueSize) * 0.25f;
        const float predictedVram = static_cast<float>(input.currentVramUsedMb) + estimatedDelta;

        // 5. Proactive LOD bias to avoid hard paging stalls near the budget.
        float proactiveLod = 0.0f;
        if (input.currentVramBudgetMb > 0) {
            const float threshold = 0.88f * static_cast<float>(input.currentVramBudgetMb);
            if (predictedVram > threshold) {
                const float excess = predictedVram - threshold;
                const float headroom = 0.12f * static_cast<float>(input.currentVramBudgetMb);
                proactiveLod = std::clamp(excess / (headroom > 1.0f ? headroom : 1.0f), 0.0f, 1.0f);
            }
        }

        const auto end = std::chrono::steady_clock::now();
        auto elapsedUs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        if (elapsedUs == 0) {
            elapsedUs = 1; // sub-microsecond work still took a measurable slice
        }

        output.recommendedLookaheadDistance = lookahead;
        output.prefetchPriorityMultiplier = priorityMultiplier;
        output.predictedShortTermVramMb = predictedVram;
        output.proactiveLodBias = proactiveLod;
        output.inferenceTimeMicroseconds = elapsedUs;
        output.activeBackend = SPM_BACKEND_CPU_CONVENTIONAL;
        return true;
    }

private:
    bool m_initialized{false};
};

class SpmNeuralAssistOrchestrator {
public:
    static SpmNeuralAssistOrchestrator& Instance() {
        static SpmNeuralAssistOrchestrator instance;
        return instance;
    }

    int Initialize(SpmNeuralAssistMode mode) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_mode = NormalizeMode(mode);

        m_backend = std::make_unique<SpmCpuPredictor>();
        if (!m_backend->Initialize()) {
            m_backend.reset();
            return 0;
        }
        m_initialized = true;
        return 1;
    }

    void Shutdown() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_backend) {
            m_backend->Shutdown();
            m_backend.reset();
        }
        m_initialized = false;
    }

    void SetMode(SpmNeuralAssistMode mode) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_mode = NormalizeMode(mode);

        if (!m_initialized) {
            return;
        }
        if (!m_backend) {
            m_backend = std::make_unique<SpmCpuPredictor>();
            m_backend->Initialize();
        }
    }

    SpmNeuralAssistMode GetMode() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_mode;
    }

    SpmBackendType GetBackendType() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (!BackendEnabled()) {
            return SPM_BACKEND_NONE;
        }
        return SPM_BACKEND_CPU_CONVENTIONAL;
    }

    const char* GetBackendName() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (!BackendEnabled()) {
            return "Disabled (neutral streaming values)";
        }
        return "Heuristic CPU predictor (no NPU inference session)";
    }

    int Predict(const SpmPredictorInput* input, SpmPredictorOutput* output) {
        if (!input || !output) {
            return 0;
        }

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (!BackendEnabled() || !m_backend) {
            output->recommendedLookaheadDistance = 100.0f;
            output->prefetchPriorityMultiplier = 1.0f;
            output->predictedShortTermVramMb = static_cast<float>(input->currentVramUsedMb);
            output->proactiveLodBias = 0.0f;
            output->inferenceTimeMicroseconds = 0;
            output->activeBackend = SPM_BACKEND_NONE;
            return 1;
        }

        return m_backend->Predict(*input, *output) ? 1 : 0;
    }

    int IsAccelerated() const {
        // No accelerated inference session exists in this build.
        return 0;
    }

    void FillBackendInfo(SpmNeuralBackendInfo* info) const {
        std::memset(info, 0, sizeof(*info));
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            info->backend = BackendEnabled() ? SPM_BACKEND_CPU_CONVENTIONAL : SPM_BACKEND_NONE;
            info->inferenceSessionActive = info->backend != SPM_BACKEND_NONE ? 1 : 0;
            std::snprintf(info->description, sizeof(info->description),
                          "%s", BackendEnabled()
                                    ? "Heuristic CPU predictor; no DirectML/ONNX inference session"
                                    : "Neural Assist disabled");
        }
        info->hardwareAccelerated = 0;

        // Hardware presence is queried, not inferred from DLL availability.
        SpmHardwareCapabilities capabilities{};
        if (SPM_DetectHardwareCapabilities(".", &capabilities) == 1) {
            info->npuHardwareDetected = capabilities.npuSupported ? 1 : 0;
        }
    }

private:
    SpmNeuralAssistOrchestrator() = default;

    static SpmNeuralAssistMode NormalizeMode(SpmNeuralAssistMode mode) {
        switch (mode) {
            case SPM_NEURAL_MODE_AUTO:
            case SPM_NEURAL_MODE_ENABLED:
            case SPM_NEURAL_MODE_DISABLED:
                return mode;
            default:
                return SPM_NEURAL_MODE_AUTO;
        }
    }

    // Caller must hold the lock.
    bool BackendEnabled() const { return m_initialized && m_mode != SPM_NEURAL_MODE_DISABLED; }

    mutable std::shared_mutex m_mutex;
    SpmNeuralAssistMode m_mode{SPM_NEURAL_MODE_AUTO};
    std::unique_ptr<SpmCpuPredictor> m_backend;
    bool m_initialized{false};
};

} // namespace

extern "C" {

SPM_NEURAL_API int SPM_NeuralAssistInit(SpmNeuralAssistMode mode) {
    return SpmNeuralAssistOrchestrator::Instance().Initialize(mode);
}

SPM_NEURAL_API void SPM_NeuralAssistShutdown(void) {
    SpmNeuralAssistOrchestrator::Instance().Shutdown();
}

SPM_NEURAL_API void SPM_NeuralAssistSetMode(SpmNeuralAssistMode mode) {
    SpmNeuralAssistOrchestrator::Instance().SetMode(mode);
}

SPM_NEURAL_API SpmNeuralAssistMode SPM_NeuralAssistGetMode(void) {
    return SpmNeuralAssistOrchestrator::Instance().GetMode();
}

SPM_NEURAL_API SpmBackendType SPM_NeuralAssistGetActiveBackend(void) {
    return SpmNeuralAssistOrchestrator::Instance().GetBackendType();
}

SPM_NEURAL_API const char* SPM_NeuralAssistGetBackendName(void) {
    return SpmNeuralAssistOrchestrator::Instance().GetBackendName();
}

SPM_NEURAL_API int SPM_NeuralAssistPredict(const SpmPredictorInput* input, SpmPredictorOutput* output) {
    return SpmNeuralAssistOrchestrator::Instance().Predict(input, output);
}

SPM_NEURAL_API int SPM_NeuralAssistIsAccelerated(void) {
    return SpmNeuralAssistOrchestrator::Instance().IsAccelerated();
}

SPM_NEURAL_API int SPM_NeuralAssistGetBackendInfo(SpmNeuralBackendInfo* outInfo) {
    if (!outInfo) {
        return 0;
    }
    SpmNeuralAssistOrchestrator::Instance().FillBackendInfo(outInfo);
    return 1;
}

} // extern "C"
