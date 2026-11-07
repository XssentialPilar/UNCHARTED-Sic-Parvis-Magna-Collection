// SPDX-License-Identifier: GPL-2.0-or-later
// native/graphics/spm_graphics_core.cpp
// Uncharted: Sic Parvis Magna Collection — SPM Native Graphics Architecture Core Implementation
#include "unc/graphics/spm_graphics_core.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
#endif

namespace unc::graphics {

namespace {

class NullSpmBackend final : public ISpmBackend {
public:
    [[nodiscard]] core::GraphicsApi GetApi() const noexcept override {
        return core::GraphicsApi::Vulkan13;
    }
    [[nodiscard]] std::string_view GetApiName() const noexcept override {
        return "NullBackend";
    }
    [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

    HRESULT Initialize(uint32_t width, uint32_t height, void* /*native_window*/) override {
        width_ = width;
        height_ = height;
        initialized_ = true;
        return S_OK;
    }
    void Shutdown() noexcept override { initialized_ = false; }
    void BeginFrame() noexcept override {}
    void EndFrame() noexcept override {}
    void ExecuteDraw(const SpmDrawPacket& /*packet*/) noexcept override {}
    void Present(uint32_t /*sync_interval*/) noexcept override {}
    void SetViewport(double /*x*/, double /*y*/, double /*w*/, double /*h*/, double /*min_z*/, double /*max_z*/) noexcept override {}
    void SetScissor(int32_t /*l*/, int32_t /*t*/, int32_t /*r*/, int32_t /*b*/) noexcept override {}

private:
    uint32_t width_{1280};
    uint32_t height_{720};
    bool initialized_{false};
};

class NativeDx9SpmBackend final : public ISpmBackend {
public:
    [[nodiscard]] core::GraphicsApi GetApi() const noexcept override {
        return core::GraphicsApi::DirectX9;
    }
    [[nodiscard]] std::string_view GetApiName() const noexcept override {
        return "DirectX9Ex";
    }
    [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

    HRESULT Initialize(uint32_t width, uint32_t height, void* native_window) override {
        width_ = width;
        height_ = height;
        hwnd_ = static_cast<HWND>(native_window);

#ifdef _WIN32
        if (hwnd_) {
            HMODULE d3d9_mod = GetModuleHandleW(L"d3d9.dll");
            if (!d3d9_mod) d3d9_mod = LoadLibraryW(L"d3d9.dll");
            if (d3d9_mod) {
                using Direct3DCreate9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
                auto create9ex = reinterpret_cast<Direct3DCreate9ExFn>(
                    GetProcAddress(d3d9_mod, "Direct3DCreate9Ex"));
                if (create9ex) {
                    IDirect3D9Ex* pD3D = nullptr;
                    if (SUCCEEDED(create9ex(D3D_SDK_VERSION, &pD3D)) && pD3D) {
                        d3d9_ex_ = pD3D;
                        D3DPRESENT_PARAMETERS d3dpp{};
                        d3dpp.BackBufferWidth = width_;
                        d3dpp.BackBufferHeight = height_;
                        d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
                        d3dpp.BackBufferCount = 2;
                        d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
                        d3dpp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
                        d3dpp.hDeviceWindow = hwnd_;
                        d3dpp.Windowed = TRUE;
                        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

                        IDirect3DDevice9Ex* pDev = nullptr;
                        HRESULT hr = d3d9_ex_->CreateDeviceEx(
                            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd_,
                            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                            &d3dpp, nullptr, &pDev);
                        if (FAILED(hr)) {
                            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
                            hr = d3d9_ex_->CreateDeviceEx(
                                D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd_,
                                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                                &d3dpp, nullptr, &pDev);
                        }
                        if (SUCCEEDED(hr) && pDev) {
                            d3d9_device_ = pDev;
                        }
                    }
                }
            }
        }
#endif
        initialized_ = true;
        return S_OK;
    }

    void Shutdown() noexcept override {
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

    void BeginFrame() noexcept override {
#ifdef _WIN32
        if (d3d9_device_) {
            d3d9_device_->BeginScene();
        }
#endif
    }

    void EndFrame() noexcept override {
#ifdef _WIN32
        if (d3d9_device_) {
            d3d9_device_->EndScene();
        }
#endif
    }

    void ExecuteDraw(const SpmDrawPacket& packet) noexcept override {
#ifdef _WIN32
        if (!d3d9_device_) return;
        D3DPRIMITIVETYPE pt = D3DPT_TRIANGLELIST;
        switch (packet.topology) {
        case SpmPrimitiveTopology::TriangleList: pt = D3DPT_TRIANGLELIST; break;
        case SpmPrimitiveTopology::TriangleStrip: pt = D3DPT_TRIANGLESTRIP; break;
        case SpmPrimitiveTopology::LineList: pt = D3DPT_LINELIST; break;
        case SpmPrimitiveTopology::LineStrip: pt = D3DPT_LINESTRIP; break;
        case SpmPrimitiveTopology::PointList: pt = D3DPT_POINTLIST; break;
        }

        if (packet.is_indexed && packet.index_count > 0) {
            const UINT prim_count = packet.index_count / 3;
            if (prim_count > 0) {
                d3d9_device_->DrawIndexedPrimitive(
                    pt, packet.vertex_offset, 0, packet.index_count, packet.first_index, prim_count);
            }
        } else if (packet.index_count > 0) {
            const UINT prim_count = packet.index_count / 3;
            if (prim_count > 0) {
                d3d9_device_->DrawPrimitive(pt, packet.first_index, prim_count);
            }
        }
#else
        (void)packet;
#endif
    }

    void Present(uint32_t /*sync_interval*/) noexcept override {
#ifdef _WIN32
        if (d3d9_device_) {
            d3d9_device_->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
        }
#endif
    }

    void SetViewport(double x, double y, double width, double height, double min_z, double max_z) noexcept override {
#ifdef _WIN32
        if (d3d9_device_) {
            D3DVIEWPORT9 vp{};
            vp.X = static_cast<DWORD>(x);
            vp.Y = static_cast<DWORD>(y);
            vp.Width = static_cast<DWORD>(width);
            vp.Height = static_cast<DWORD>(height);
            vp.MinZ = static_cast<float>(min_z);
            vp.MaxZ = static_cast<float>(max_z);
            d3d9_device_->SetViewport(&vp);
        }
#else
        (void)x; (void)y; (void)width; (void)height; (void)min_z; (void)max_z;
#endif
    }

    void SetScissor(int32_t left, int32_t top, int32_t right, int32_t bottom) noexcept override {
#ifdef _WIN32
        if (d3d9_device_) {
            RECT r{left, top, right, bottom};
            d3d9_device_->SetScissorRect(&r);
        }
#else
        (void)left; (void)top; (void)right; (void)bottom;
#endif
    }

private:
    uint32_t width_{1280};
    uint32_t height_{720};
    HWND hwnd_{nullptr};
    bool initialized_{false};
#ifdef _WIN32
    IDirect3D9Ex* d3d9_ex_{nullptr};
    IDirect3DDevice9Ex* d3d9_device_{nullptr};
#endif
};

} // namespace

SpmGraphicsCore& SpmGraphicsCore::Instance() noexcept {
    static SpmGraphicsCore instance;
    return instance;
}

HRESULT SpmGraphicsCore::Initialize(core::GraphicsApi preferred_api, uint32_t width,
                                    uint32_t height, void* hwnd) {
    Shutdown();
    switch (preferred_api) {
    case core::GraphicsApi::DirectX9:
        active_backend_ = std::make_unique<NativeDx9SpmBackend>();
        break;
    default:
        active_backend_ = std::make_unique<NullSpmBackend>();
        break;
    }

    if (active_backend_) {
        const HRESULT hr = active_backend_->Initialize(width, height, hwnd);
        if (FAILED(hr)) {
            active_backend_.reset();
            return hr;
        }
    }

    state_cache_.InvalidateAll();
    is_initialized_ = true;
    return S_OK;
}

void SpmGraphicsCore::Shutdown() noexcept {
    if (active_backend_) {
        active_backend_->Shutdown();
        active_backend_.reset();
    }
    is_initialized_ = false;
}

void SpmGraphicsCore::RecordDraw(const SpmDrawPacket& packet) noexcept {
    total_draw_calls_.fetch_add(1, std::memory_order_relaxed);
    if (active_backend_) {
        active_backend_->ExecuteDraw(packet);
    }
}

void SpmGraphicsCore::RecordDispatch(uint32_t /*groups_x*/, uint32_t /*groups_y*/,
                                     uint32_t /*groups_z*/) noexcept {
    total_dispatches_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace unc::graphics
