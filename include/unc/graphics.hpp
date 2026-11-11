// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Graphics Backends, Viewport Math & Swapchain Hook
#pragma once

#include "unc/core.hpp"
#include "minhook.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

namespace unc::graphics {

enum class AspectRatioPreset : uint8_t {
    NativeWindow = 0,
    Standard_16_9 = 1,
    Classic_16_10 = 2,
    Ultrawide_21_9 = 3,
    SuperUltrawide_32_9 = 4,
};

enum class SwapchainDirtyFlags : uint32_t {
    None = 0U,
    ClientSizeChanged = 1U << 0U,
    InternalResolutionChanged = 1U << 1U,
    AspectRatioChanged = 1U << 2U,
    ColorSpaceChanged = 1U << 3U,
    HudScaleChanged = 1U << 4U,
};

[[nodiscard]] constexpr SwapchainDirtyFlags operator|(SwapchainDirtyFlags a,
                                                      SwapchainDirtyFlags b) noexcept {
    return static_cast<SwapchainDirtyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr SwapchainDirtyFlags operator&(SwapchainDirtyFlags a,
                                                      SwapchainDirtyFlags b) noexcept {
    return static_cast<SwapchainDirtyFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr bool HasDirtyFlag(SwapchainDirtyFlags mask,
                                          SwapchainDirtyFlags flag) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0U;
}

struct ViewportRect {
    double x{0.0};
    double y{0.0};
    double width{1280.0};
    double height{720.0};
    double min_depth{0.0};
    double max_depth{1.0};
};

struct ScissorRect {
    int32_t left{0};
    int32_t top{0};
    int32_t right{1280};
    int32_t bottom{720};
};

struct SwapchainDescSnapshot {
    uint32_t buffer_width{1280};
    uint32_t buffer_height{720};
    uint32_t client_width{1280};
    uint32_t client_height{720};
    uint32_t refresh_rate_hz{60};
    uint32_t buffer_count{2};
    bool windowed{true};
    bool allow_tearing{false};
};

struct ProjectionCompensation {
    double logical_aspect_ratio{16.0 / 9.0};
    double horizontal_fov_scale{1.0};
    double vertical_fov_scale{1.0};
    double hud_safe_scale_x{1.0};
    double hud_safe_scale_y{1.0};
    std::array<float, 16> clip_scale_matrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
};

enum class PresentPipelineStep : uint8_t {
    ReadSwapchainDescriptor = 1,
    CheckAndClearDirtyFlags = 2,
    ApplyProjectionAndUiScale = 3,
    RenderCustomOverlays = 4,
    InvokeOriginalPresent = 5,
};

struct PresentExecutionTrace {
    std::array<PresentPipelineStep, 5> executed_steps{};
    size_t step_count{0};
    SwapchainDirtyFlags consumed_dirty_flags{SwapchainDirtyFlags::None};
    ViewportRect active_viewport{};
    ScissorRect active_scissor{};
    ProjectionCompensation active_projection{};
    HRESULT present_result{S_OK};
};

// Invariant 2.1: Logical Aspect Ratio
// Aspect Ratio = Width_logical / Height_logical
[[nodiscard]] constexpr double ComputeLogicalAspectRatio(uint32_t width_logical,
                                                         uint32_t height_logical) noexcept {
    if (width_logical == 0U || height_logical == 0U) {
        return 16.0 / 9.0;
    }
    return static_cast<double>(width_logical) / static_cast<double>(height_logical);
}

[[nodiscard]] ViewportRect ComputeDecoupledViewport(uint32_t client_width, uint32_t client_height,
                                                    double target_aspect_ratio) noexcept;
[[nodiscard]] ScissorRect ComputeScissorFromViewport(const ViewportRect& viewport) noexcept;
[[nodiscard]] ProjectionCompensation ComputeProjectionCompensation(uint32_t logical_width,
                                                                   uint32_t logical_height,
                                                                   double baseline_aspect = 16.0 / 9.0) noexcept;

class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;

    [[nodiscard]] virtual core::GraphicsApi GetApi() const noexcept = 0;
    [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;
    virtual HRESULT Initialize(uint32_t internal_width, uint32_t internal_height,
                               bool low_vram_mode) = 0;
    virtual void ApplyViewportAndScissor(const ViewportRect& viewport,
                                         const ScissorRect& scissor) noexcept = 0;
    virtual void ApplyProjectionCompensation(
        const ProjectionCompensation& compensation) noexcept = 0;
    virtual void RenderOverlayPass() noexcept = 0;
    [[nodiscard]] virtual ViewportRect GetActiveViewport() const noexcept = 0;
    [[nodiscard]] virtual ScissorRect GetActiveScissor() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IGraphicsBackend> CreateDx9Backend();
[[nodiscard]] std::unique_ptr<IGraphicsBackend> CreateDx11Backend();
[[nodiscard]] std::unique_ptr<IGraphicsBackend> CreateDx12Backend();

class SwapchainHook final {
public:
    using DescriptorReaderFn = std::function<HRESULT(SwapchainDescSnapshot&)>;
    using OriginalPresentFn = std::function<HRESULT(uint32_t sync_interval, uint32_t flags)>;
    using OverlayRenderFn = std::function<void(const SwapchainDescSnapshot&, const ViewportRect&)>;

    explicit SwapchainHook(IGraphicsBackend* backend = nullptr) noexcept;

    void SetBackend(IGraphicsBackend* backend) noexcept;
    void SetDescriptorReader(DescriptorReaderFn reader);
    void SetOriginalPresent(OriginalPresentFn present_fn);
    void SetOverlayRenderer(OverlayRenderFn overlay_fn);
    using StreamingTimeSliceFn = void (*)(std::size_t maxUploadBytes);
    using StreamingBudgetEnforceFn = void (*)(std::size_t vramBudgetMb);

    void SetStreamingHooks(StreamingTimeSliceFn timeSliceFn, StreamingBudgetEnforceFn budgetFn) noexcept {
        streaming_time_slice_ = timeSliceFn;
        streaming_budget_enforce_ = budgetFn;
    }
    [[nodiscard]] StreamingTimeSliceFn GetStreamingTimeSliceHook() const noexcept {
        return streaming_time_slice_;
    }
    [[nodiscard]] StreamingBudgetEnforceFn GetStreamingBudgetHook() const noexcept {
        return streaming_budget_enforce_;
    }

    void MarkDirty(SwapchainDirtyFlags flags) noexcept;
    [[nodiscard]] SwapchainDirtyFlags PeekDirtyFlags() const noexcept;

    // Enforces Invariant 2.3 strict 5-step execution order.
    [[nodiscard]] HRESULT ExecutePresentPipeline(uint32_t sync_interval, uint32_t present_flags,
                                                 PresentExecutionTrace* out_trace = nullptr);

    [[nodiscard]] const SwapchainDescSnapshot& LastDescriptor() const noexcept {
        return last_desc_;
    }
    [[nodiscard]] const ViewportRect& LastViewport() const noexcept { return last_viewport_; }
    [[nodiscard]] const ProjectionCompensation& LastProjection() const noexcept {
        return last_projection_;
    }

private:
    IGraphicsBackend* backend_{nullptr};
    std::atomic<uint32_t> dirty_flags_{
        static_cast<uint32_t>(SwapchainDirtyFlags::ClientSizeChanged |
                              SwapchainDirtyFlags::InternalResolutionChanged |
                              SwapchainDirtyFlags::AspectRatioChanged)};
    SwapchainDescSnapshot last_desc_{};
    ViewportRect last_viewport_{};
    ScissorRect last_scissor_{};
    ProjectionCompensation last_projection_{};
    DescriptorReaderFn descriptor_reader_{};
    OriginalPresentFn original_present_{};
    OverlayRenderFn overlay_renderer_{};
    StreamingTimeSliceFn streaming_time_slice_{nullptr};
    StreamingBudgetEnforceFn streaming_budget_enforce_{nullptr};
};

} // namespace unc::graphics

#include "unc/graphics/dx11_backend.hpp"
#include "unc/graphics/dynamic_ring_buffer.hpp"
#include "unc/graphics/transient_rt_pool.hpp"
#include "unc/graphics/vram_budget_mgr.hpp"

namespace unc::graphics {

// Section 5.3 Strict 7-Step IDXGISwapChain::Present Execution Order
enum class DeterministicPresentStep : std::uint8_t {
    Step1_BeginFrameQpcTimer = 1,
    Step2_PollVramAndEnforceLru = 2,
    Step3_ProcessTimeSlicedUploadQueue = 3,
    Step4_EvaluateDirtyFlagsAndRingBufferConstants = 4,
    Step5_RenderAdaptedUiAndHud = 5,
    Step6_ResetFrameLinearArena = 6,
    Step7_HybridFramePaceAndOriginalPresent = 7,
};

struct DeterministicPresentTrace {
    std::array<DeterministicPresentStep, 7> executedSteps{};
    std::size_t   stepCount{0};
    VramTelemetry vramTelemetry{};
    std::size_t   uploadedBytesThisFrame{0};
    std::size_t   arenaUsedBeforeReset{0};
    std::size_t   arenaUsedAfterReset{0};
    RingMapMode   constantBufferMapMode{RingMapMode::WriteNoOverwrite};
    HRESULT       presentResult{S_OK};
};

[[nodiscard]] HRESULT ExecuteDeterministic7StepPresent(
    std::uint64_t frameIndex,
    std::uint32_t syncInterval,
    std::uint32_t presentFlags,
    core::FramePacer& framePacer,
    VramBudgetManager& vramMgr,
    core::SpscQueue<std::size_t, 64>& pendingTextureUploadQueue,
    DynamicRingBuffer& ringBuffer,
    core::LinearArenaAllocator& frameArena,
    SwapchainHook& swapchainHook,
    DeterministicPresentTrace* outTrace = nullptr) noexcept;

} // namespace unc::graphics
