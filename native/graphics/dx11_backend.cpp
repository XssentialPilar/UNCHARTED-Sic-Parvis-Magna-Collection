// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — DirectX 11 (Feature Level 11_0) Backend
// Targets 2015+ Entry & Mid-Range GPUs: GeForce GTX 460/550 Ti/650/750 Ti/660,
// Radeon HD 5770/6790/7750/7850/R7 265/R9 270.

#include "unc/graphics.hpp"

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#endif

namespace unc::graphics {

namespace {

class Dx11Backend final : public IGraphicsBackend {
public:
    Dx11Backend() noexcept = default;
    ~Dx11Backend() override = default;

    [[nodiscard]] core::GraphicsApi GetApi() const noexcept override {
        return core::GraphicsApi::DirectX11_FL11_0;
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
    uint32_t internal_width_{1280U};
    uint32_t internal_height_{720U};
    bool low_vram_mode_{true};
    bool initialized_{false};
    uint64_t overlay_frames_rendered_{0ULL};
    ViewportRect active_viewport_{};
    ScissorRect active_scissor_{};
    ProjectionCompensation active_projection_{};
};

} // namespace

std::unique_ptr<IGraphicsBackend> CreateDx11Backend() {
    return std::make_unique<Dx11Backend>();
}

} // namespace unc::graphics
