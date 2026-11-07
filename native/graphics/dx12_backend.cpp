// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — DirectX 12 Backend with Feature Level 11_0 Fallback

#include "unc/graphics.hpp"
#include "d3d12_agility.h"

namespace unc::graphics {

namespace {

class Dx12Backend final : public IGraphicsBackend {
public:
    Dx12Backend() noexcept = default;
    ~Dx12Backend() override = default;

    [[nodiscard]] core::GraphicsApi GetApi() const noexcept override {
        return core::GraphicsApi::DirectX12;
    }

    [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

    HRESULT Initialize(uint32_t internal_width, uint32_t internal_height,
                       bool low_vram_mode) override {
        if (internal_width == 0U || internal_height == 0U) {
            THROW_IF_FAILED(E_INVALIDARG);
        }
        internal_width_ = internal_width;
        internal_height_ = internal_height;
        low_vram_mode_ = low_vram_mode;
        agility_config_.sdk_version = vendor::d3d12::kAgilitySdkVersion;
        agility_config_.sdk_path = vendor::d3d12::kAgilitySdkPath;
        active_viewport_ = ComputeDecoupledViewport(
            internal_width, internal_height,
            ComputeLogicalAspectRatio(internal_width, internal_height));
        active_scissor_ = ComputeScissorFromViewport(active_viewport_);
        active_projection_ = ComputeProjectionCompensation(internal_width, internal_height);
        initialized_ = true;
        return S_OK;
    }

    void ApplyViewportAndScissor(const ViewportRect& viewport,
                                 const ScissorRect& scissor) noexcept override {
        active_viewport_ = viewport;
        active_scissor_ = scissor;
    }

    void ApplyProjectionCompensation(
        const ProjectionCompensation& compensation) noexcept override {
        active_projection_ = compensation;
    }

    void RenderOverlayPass() noexcept override { ++overlay_frames_rendered_; }

    [[nodiscard]] ViewportRect GetActiveViewport() const noexcept override {
        return active_viewport_;
    }

    [[nodiscard]] ScissorRect GetActiveScissor() const noexcept override {
        return active_scissor_;
    }

private:
    uint32_t internal_width_{1920U};
    uint32_t internal_height_{1080U};
    bool low_vram_mode_{false};
    bool initialized_{false};
    uint64_t overlay_frames_rendered_{0ULL};
    vendor::d3d12::AgilitySdkConfig agility_config_{};
    ViewportRect active_viewport_{};
    ScissorRect active_scissor_{};
    ProjectionCompensation active_projection_{};
};

} // namespace

std::unique_ptr<IGraphicsBackend> CreateDx12Backend() {
    return std::make_unique<Dx12Backend>();
}

} // namespace unc::graphics
