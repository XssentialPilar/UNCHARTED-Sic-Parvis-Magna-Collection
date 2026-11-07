// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Direct3D 9 / 9Ex Native Backend Implementation
// Targets Legacy DirectX 9.0c Hardware (Shader Model 3.0) & Direct3D 9Ex Low-Latency FlipEx

#include "unc/graphics.hpp"
#include "unc/graphics/dx9_backend.hpp"

#ifdef _WIN32
#include <d3d9.h>
#endif

namespace unc::graphics {

namespace {

class Dx9Backend final : public IGraphicsBackend {
public:
    Dx9Backend() noexcept = default;
    ~Dx9Backend() override {
        Shutdown();
    }

    [[nodiscard]] core::GraphicsApi GetApi() const noexcept override {
        return core::GraphicsApi::DirectX9;
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

#ifdef _WIN32
        // Attempt Direct3D 9Ex device initialization
        HMODULE d3d9_mod = GetModuleHandleW(L"d3d9.dll");
        if (!d3d9_mod) {
            d3d9_mod = LoadLibraryW(L"d3d9.dll");
        }
        if (d3d9_mod) {
            using Direct3DCreate9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
            auto create9ex = reinterpret_cast<Direct3DCreate9ExFn>(
                GetProcAddress(d3d9_mod, "Direct3DCreate9Ex"));
            if (create9ex) {
                IDirect3D9Ex* pD3D9Ex = nullptr;
                if (SUCCEEDED(create9ex(D3D_SDK_VERSION, &pD3D9Ex)) && pD3D9Ex) {
                    d3d9_ex_ = pD3D9Ex;
                    d3d9_ex_supported_ = true;
                }
            }
        }
#endif

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

    void RenderOverlayPass() noexcept override {
        ++overlay_frames_rendered_;
    }

    [[nodiscard]] ViewportRect GetActiveViewport() const noexcept override {
        return active_viewport_;
    }

    [[nodiscard]] ScissorRect GetActiveScissor() const noexcept override {
        return active_scissor_;
    }

private:
    void Shutdown() noexcept {
#ifdef _WIN32
        if (d3d9_device_) {
            d3d9_device_->Release();
            d3d9_device_ = nullptr;
        }
        if (d3d9_ex_) {
            d3d9_ex_->Release();
            d3d9_ex_ = nullptr;
        }
#endif
        initialized_ = false;
    }

    uint32_t internal_width_{1280U};
    uint32_t internal_height_{720U};
    bool low_vram_mode_{true};
    bool initialized_{false};
    bool d3d9_ex_supported_{false};
    uint64_t overlay_frames_rendered_{0ULL};
    ViewportRect active_viewport_{};
    ScissorRect active_scissor_{};
    ProjectionCompensation active_projection_{};

#ifdef _WIN32
    IDirect3D9Ex* d3d9_ex_{nullptr};
    IDirect3DDevice9Ex* d3d9_device_{nullptr};
#endif
};

} // namespace

std::unique_ptr<IGraphicsBackend> CreateDx9Backend() {
    return std::make_unique<Dx9Backend>();
}

} // namespace unc::graphics
