// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection
// Hardware DXGI Flip-Model Presentation Bridge (DirectX 11 / DirectX 12) + Deterministic Sub-ms
// Hybrid Frame Pacer (QPC + High-Resolution Waitable Timer + _mm_pause).

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace Gow3Dxgi {

enum class Api : int {
    Vulkan = 0,
    DirectX11 = 1,
    DirectX12 = 2,
    DirectX9 = 3,
};

enum class PacerMode : int {
    Hybrid = 0,
    Timer = 1,
    Spin = 2,
    Off = 3,
};

Api ParseApi(std::string_view text) noexcept;
const char* ApiConfigName(Api api) noexcept;
const char* ApiOverlayTag(Api api) noexcept;

PacerMode ParsePacerMode(std::string_view text) noexcept;
const char* PacerModeName(PacerMode mode) noexcept;

/// Returns the user's requested presentation API (env GOW3_GRAPHICS_API -> Gow3Settings).
Api RequestedApi() noexcept;

/// Returns the currently active presentation API (reflects fallback if hardware init failed).
Api ActiveApi() noexcept;
void SetActiveApi(Api api) noexcept;

/// Deterministic zero-allocation frame pacer using QueryPerformanceCounter + Win32
/// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION + _mm_pause() spin-lock tail.
class FramePacer {
public:
    FramePacer() noexcept;
    ~FramePacer() noexcept;

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    void Reset() noexcept;
    void Wait(int target_fps, bool vsync, double display_hz, PacerMode mode) noexcept;

private:
    void* waitable_timer = nullptr;
    int64_t qpc_freq = 0;
    double next_deadline_ticks = 0.0;
    int last_target_fps = 0;
};

/// Native hardware DirectX 11 / DirectX 12 swapchain presenter with zero-copy NT-handle
/// VK_KHR_external_memory_win32 texture sharing and IDXGIAdapter3 VRAM budget telemetry.
class PresenterBridge {
public:
    static constexpr uint32_t kBufferCount = 3;

    PresenterBridge() noexcept;
    ~PresenterBridge() noexcept;

    PresenterBridge(const PresenterBridge&) = delete;
    PresenterBridge& operator=(const PresenterBridge&) = delete;

    /// Initializes the D3D11 or D3D12 device matched by LUID to `vk_instance` and creates the
    /// hardware DXGI flip-discard swapchain on `hwnd` (pass nullptr for headless unit tests).
    bool Initialize(const Vulkan::Instance& vk_instance, void* hwnd, uint32_t width,
                    uint32_t height, Api requested_api) noexcept;

    /// Allocates `kBufferCount` shared D3D11/D3D12 textures and imports them zero-copy into
    /// Vulkan `VkImage` + `VkImageView` handles for direct rendering/blitting.
    bool CreateSharedImages(uint32_t width, uint32_t height, vk::Format view_format,
                            std::vector<vk::Image>& out_images,
                            std::vector<vk::ImageView>& out_views) noexcept;

    /// Destroys imported Vulkan images/views/memories and releases the shared D3D textures.
    void ReleaseSharedImages(std::vector<vk::Image>& images,
                             std::vector<vk::ImageView>& views) noexcept;

    /// Resizes the DXGI swapchain and rebuilds the shared Vulkan <-> D3D images.
    bool Resize(uint32_t width, uint32_t height, vk::Format view_format,
                std::vector<vk::Image>& out_images,
                std::vector<vk::ImageView>& out_views) noexcept;

    /// Copies the finished shared texture `image_index` to the DXGI backbuffer and presents via
    /// D3D11 or D3D12 (or copies to an offscreen target in headless test mode).
    bool Present(uint32_t image_index, bool vsync, FramePacer& pacer, int target_fps,
                 double display_hz, PacerMode pacer_mode) noexcept;

    /// Queries live local VRAM usage and OS budget via IDXGIAdapter3::QueryVideoMemoryInfo.
    bool QueryLocalVram(uint64_t& out_usage_bytes, uint64_t& out_budget_bytes) const noexcept;

    Api GetApi() const noexcept { return active_api; }
    bool IsInitialized() const noexcept { return initialized; }
    bool UsesZeroCopyInterop() const noexcept { return zero_copy_interop; }

private:
    struct Impl;
    Impl* impl = nullptr;
    const Vulkan::Instance* instance = nullptr;
    Api active_api = Api::Vulkan;
    bool initialized = false;
    bool zero_copy_interop = false;
};

} // namespace Gow3Dxgi
