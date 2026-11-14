// gow3: shadPS4's frame/register dump tooling is not part of this port.
#pragma once
#include <span>
#include <string>
#include <vector>
#include <atomic>
#include "common/types.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/regs.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace DebugStateType {
enum class QueueType { dcb = 0, ccb = 1, acb = 2 };
struct QueueDump {
    QueueType type;
    u32 submit_num;
    u32 num2;
    std::vector<u32> data;
    uintptr_t base_addr;
};
class DebugStateImpl {
public:
    using CsState = AmdGpu::ComputeProgram;
    bool is_using_fsr{};
    std::atomic<uint32_t> draw_calls{0};
    std::atomic<uint32_t> dispatches{0};
    std::atomic<uint32_t> flip_frames{0};

    void IncFlipFrameNum() noexcept { flip_frames.fetch_add(1, std::memory_order_relaxed); }
    void IncDrawCall() noexcept { draw_calls.fetch_add(1, std::memory_order_relaxed); }
    void IncDispatch() noexcept { dispatches.fetch_add(1, std::memory_order_relaxed); }
    void IncGnmFrameNum() noexcept {}
    u32 GetFrameNum() const noexcept { return flip_frames.load(std::memory_order_relaxed); }
    uint32_t GetDrawCallsAndReset() noexcept { return draw_calls.exchange(0, std::memory_order_relaxed); }
    uint32_t GetDispatchesAndReset() noexcept { return dispatches.exchange(0, std::memory_order_relaxed); }
    bool DumpingCurrentFrame() const { return false; }
    bool DumpingCurrentReg() { return false; }
    bool ShouldPauseInSubmit() const { return false; }
    void PushQueueDump(QueueDump) {}
    void PushRegsDump(uintptr_t, uintptr_t, const AmdGpu::Regs&) {}
    void PushRegsDumpCompute(uintptr_t, uintptr_t, const CsState&) {}
    void CollectShader(const std::string&, Shader::SwStage, vk::ShaderModule, std::span<const u32>,
                       std::span<const u32>, std::span<const u32>, bool) {}
    void ShowDebugMessage(std::string) {}
    bool IsGuestThreadsPaused() const { return false; }
    void PauseGuestThreads() {}
    void ResumeGuestThreads() {}
};
} // namespace DebugStateType
inline DebugStateType::DebugStateImpl DebugStateInstance;
#define DebugState DebugStateInstance
