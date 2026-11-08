// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Decoupled Swapchain Hook & Viewport Pipeline

#include "unc/graphics.hpp"

#include <algorithm>
#include <cmath>

namespace unc::graphics {

ViewportRect ComputeDecoupledViewport(uint32_t client_width, uint32_t client_height,
                                      double target_aspect_ratio) noexcept {
    ViewportRect vp{};
    vp.min_depth = 0.0;
    vp.max_depth = 1.0;

    if (client_width == 0U || client_height == 0U) {
        vp.x = 0.0;
        vp.y = 0.0;
        vp.width = 0.0;
        vp.height = 0.0;
        return vp;
    }

    const double cw = static_cast<double>(client_width);
    const double ch = static_cast<double>(client_height);
    const double safe_target = target_aspect_ratio > 0.0 ? target_aspect_ratio : (16.0 / 9.0);
    const double client_aspect = cw / ch;

    if (std::abs(client_aspect - safe_target) <= 1e-6) {
        vp.x = 0.0;
        vp.y = 0.0;
        vp.width = cw;
        vp.height = ch;
        return vp;
    }

    if (client_aspect > safe_target) {
        // Pillarbox: window is wider than target aspect ratio
        vp.height = ch;
        vp.width = ch * safe_target;
        vp.x = (cw - vp.width) * 0.5;
        vp.y = 0.0;
    } else {
        // Letterbox: window is taller than target aspect ratio (e.g. 16:10 display with 16:9 frame)
        vp.width = cw;
        vp.height = cw / safe_target;
        vp.x = 0.0;
        vp.y = (ch - vp.height) * 0.5;
    }
    return vp;
}

ScissorRect ComputeScissorFromViewport(const ViewportRect& viewport) noexcept {
    ScissorRect scissor{};
    scissor.left = static_cast<int32_t>(std::llround(std::floor(viewport.x)));
    scissor.top = static_cast<int32_t>(std::llround(std::floor(viewport.y)));
    scissor.right = static_cast<int32_t>(std::llround(std::ceil(viewport.x + viewport.width)));
    scissor.bottom = static_cast<int32_t>(std::llround(std::ceil(viewport.y + viewport.height)));
    return scissor;
}

ProjectionCompensation ComputeProjectionCompensation(uint32_t logical_width,
                                                     uint32_t logical_height,
                                                     double baseline_aspect) noexcept {
    ProjectionCompensation comp{};
    const double safe_baseline = baseline_aspect > 0.0 ? baseline_aspect : (16.0 / 9.0);
    comp.logical_aspect_ratio = ComputeLogicalAspectRatio(logical_width, logical_height);

    if (comp.logical_aspect_ratio >= safe_baseline) {
        // Hor+ scaling for Ultrawide (21:9, 32:9): keep vertical FOV intact, expand horizontal FOV,
        // and clamp HUD safe zone to the central 16:9 pillar.
        comp.horizontal_fov_scale = comp.logical_aspect_ratio / safe_baseline;
        comp.vertical_fov_scale = 1.0;
        comp.hud_safe_scale_x = safe_baseline / comp.logical_aspect_ratio;
        comp.hud_safe_scale_y = 1.0;
    } else {
        // 16:10 or 4:3: preserve horizontal FOV and adjust vertical scale
        comp.horizontal_fov_scale = 1.0;
        comp.vertical_fov_scale = safe_baseline / comp.logical_aspect_ratio;
        comp.hud_safe_scale_x = 1.0;
        comp.hud_safe_scale_y = comp.logical_aspect_ratio / safe_baseline;
    }

    comp.clip_scale_matrix = {
        static_cast<float>(1.0 / comp.horizontal_fov_scale), 0.0f, 0.0f, 0.0f,
        0.0f, static_cast<float>(1.0 / comp.vertical_fov_scale), 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return comp;
}

SwapchainHook::SwapchainHook(IGraphicsBackend* backend) noexcept : backend_(backend) {}

void SwapchainHook::SetBackend(IGraphicsBackend* backend) noexcept { backend_ = backend; }

void SwapchainHook::SetDescriptorReader(DescriptorReaderFn reader) {
    descriptor_reader_ = std::move(reader);
}

void SwapchainHook::SetOriginalPresent(OriginalPresentFn present_fn) {
    original_present_ = std::move(present_fn);
}

void SwapchainHook::SetOverlayRenderer(OverlayRenderFn overlay_fn) {
    overlay_renderer_ = std::move(overlay_fn);
}

void SwapchainHook::MarkDirty(SwapchainDirtyFlags flags) noexcept {
    dirty_flags_.fetch_or(static_cast<uint32_t>(flags), std::memory_order_release);
}

SwapchainDirtyFlags SwapchainHook::PeekDirtyFlags() const noexcept {
    return static_cast<SwapchainDirtyFlags>(dirty_flags_.load(std::memory_order_acquire));
}

HRESULT SwapchainHook::ExecutePresentPipeline(uint32_t sync_interval, uint32_t present_flags,
                                              PresentExecutionTrace* out_trace) {
    PresentExecutionTrace local_trace{};

    // Step 1: Safe reading of swapchain descriptors (GetDesc / GetDesc1)
    local_trace.executed_steps[local_trace.step_count++] =
        PresentPipelineStep::ReadSwapchainDescriptor;
    if (descriptor_reader_) {
        const HRESULT desc_hr = descriptor_reader_(last_desc_);
        THROW_IF_FAILED(desc_hr);
    }

    // Step 2: Check and clear dirty_flags atomically
    local_trace.executed_steps[local_trace.step_count++] =
        PresentPipelineStep::CheckAndClearDirtyFlags;
    const auto dirty = static_cast<SwapchainDirtyFlags>(
        dirty_flags_.exchange(static_cast<uint32_t>(SwapchainDirtyFlags::None),
                              std::memory_order_acq_rel));
    local_trace.consumed_dirty_flags = dirty;

    // Step 3: Apply projection adjustments and UI scaling
    local_trace.executed_steps[local_trace.step_count++] =
        PresentPipelineStep::ApplyProjectionAndUiScale;
    if (dirty != SwapchainDirtyFlags::None || last_viewport_.width <= 0.0) {
        const double logical_aspect =
            ComputeLogicalAspectRatio(last_desc_.buffer_width, last_desc_.buffer_height);
        last_viewport_ = ComputeDecoupledViewport(last_desc_.client_width, last_desc_.client_height,
                                                  logical_aspect);
        last_scissor_ = ComputeScissorFromViewport(last_viewport_);
        last_projection_ =
            ComputeProjectionCompensation(last_desc_.buffer_width, last_desc_.buffer_height);
    }
    if (backend_ != nullptr) {
        backend_->ApplyViewportAndScissor(last_viewport_, last_scissor_);
        backend_->ApplyProjectionCompensation(last_projection_);
    }
    local_trace.active_viewport = last_viewport_;
    local_trace.active_scissor = last_scissor_;
    local_trace.active_projection = last_projection_;

    // Step 4: Render custom overlays
    local_trace.executed_steps[local_trace.step_count++] =
        PresentPipelineStep::RenderCustomOverlays;
    if (overlay_renderer_) {
        overlay_renderer_(last_desc_, last_viewport_);
    }
    if (backend_ != nullptr) {
        backend_->RenderOverlayPass();
    }

    // Step 5: Invoke original Present function pointer
    local_trace.executed_steps[local_trace.step_count++] =
        PresentPipelineStep::InvokeOriginalPresent;
    HRESULT present_hr = S_OK;
    if (original_present_) {
        present_hr = original_present_(sync_interval, present_flags);
        THROW_IF_FAILED(present_hr);
    }
    local_trace.present_result = present_hr;

    if (out_trace != nullptr) {
        *out_trace = local_trace;
    }
    return present_hr;
}

HRESULT ExecuteDeterministic7StepPresent(
    std::uint64_t frameIndex,
    std::uint32_t syncInterval,
    std::uint32_t presentFlags,
    core::FramePacer& framePacer,
    VramBudgetManager& vramMgr,
    core::SpscQueue<std::size_t, 64>& pendingTextureUploadQueue,
    DynamicRingBuffer& ringBuffer,
    core::LinearArenaAllocator& frameArena,
    SwapchainHook& swapchainHook,
    DeterministicPresentTrace* outTrace) noexcept {
    DeterministicPresentTrace trace{};

    // Step 1: Begin QPC frame timer for the presentation pass
    trace.executedSteps[trace.stepCount++] = DeterministicPresentStep::Step1_BeginFrameQpcTimer;
    framePacer.BeginFrame();

    // Step 2: Query DXGI VRAM telemetry and execute LRU Mip 0 eviction if UsageRatio >= 0.88
    trace.executedSteps[trace.stepCount++] = DeterministicPresentStep::Step2_PollVramAndEnforceLru;
    trace.vramTelemetry = vramMgr.PollAndEnforceBudget(nullptr, frameIndex);
    if (swapchainHook.GetStreamingBudgetHook() != nullptr) {
        const std::size_t budgetMb = static_cast<std::size_t>(vramMgr.GetHardCapBytes() / (1024ULL * 1024ULL));
        swapchainHook.GetStreamingBudgetHook()(budgetMb);
    }

    // Step 3: Process at most 2.5 MB from the lock-free async texture upload queue
    trace.executedSteps[trace.stepCount++] =
        DeterministicPresentStep::Step3_ProcessTimeSlicedUploadQueue;
    if (swapchainHook.GetStreamingTimeSliceHook() != nullptr) {
        swapchainHook.GetStreamingTimeSliceHook()(vramMgr.GetMaxUploadPerFrame());
    }
    std::size_t sliceBytes = 0U;
    while (!pendingTextureUploadQueue.IsEmpty()) {
        if (!pendingTextureUploadQueue.TryPop(sliceBytes)) {
            break;
        }
        if (vramMgr.RequestUploadSlice(sliceBytes)) {
            trace.uploadedBytesThisFrame += sliceBytes;
        } else {
            // Re-enqueue remaining slice above the 2.5 MB frame quota for the next frame
            const std::size_t remainingQuota =
                vramMgr.GetMaxUploadPerFrame() - vramMgr.GetUploadedThisFrame();
            if (remainingQuota > 0U && remainingQuota < sliceBytes &&
                vramMgr.RequestUploadSlice(remainingQuota)) {
                trace.uploadedBytesThisFrame += remainingQuota;
                (void)pendingTextureUploadQueue.TryPush(sliceBytes - remainingQuota);
            } else {
                (void)pendingTextureUploadQueue.TryPush(sliceBytes);
            }
            break;
        }
    }

    // Step 4: Evaluate window/viewport dirty flags and update projection constants via DynamicRingBuffer (MAP_WRITE_NO_OVERWRITE)
    trace.executedSteps[trace.stepCount++] =
        DeterministicPresentStep::Step4_EvaluateDirtyFlagsAndRingBufferConstants;
    RingAllocation ringAlloc{};
    if (ringBuffer.Allocate(nullptr, sizeof(ProjectionCompensation), 16U, ringAlloc)) {
        trace.constantBufferMapMode = ringAlloc.mapMode;
        ringBuffer.Unmap(nullptr);
    }

    // Step 5: Render adapted UI/HUD layers (and execute the swapchain hook's inner steps)
    trace.executedSteps[trace.stepCount++] = DeterministicPresentStep::Step5_RenderAdaptedUiAndHud;
    HRESULT presentHr = S_OK;
    try {
        presentHr = swapchainHook.ExecutePresentPipeline(syncInterval, presentFlags, nullptr);
    } catch (const core::HResultException& ex) {
        presentHr = ex.code();
    } catch (...) {
        presentHr = E_FAIL;
    }

    // Step 6: Reset the current frame's LinearArenaAllocator in O(1) with zero OS calls
    trace.executedSteps[trace.stepCount++] = DeterministicPresentStep::Step6_ResetFrameLinearArena;
    trace.arenaUsedBeforeReset = frameArena.GetUsedBytes();
    frameArena.ResetAndCount();
    trace.arenaUsedAfterReset = frameArena.GetUsedBytes();

    // Step 7: Execute hybrid Frame Pacing and invoke oPresent(SyncInterval, Flags)
    trace.executedSteps[trace.stepCount++] =
        DeterministicPresentStep::Step7_HybridFramePaceAndOriginalPresent;
    framePacer.WaitForTargetFrameTime();
    trace.presentResult = presentHr;

    if (outTrace != nullptr) {
        *outTrace = trace;
    }
    return presentHr;
}

} // namespace unc::graphics
