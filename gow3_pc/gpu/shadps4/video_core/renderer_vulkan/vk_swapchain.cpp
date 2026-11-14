// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "gow3_graphics.h"
#include "gow3_settings.h"
#include "gow3_toggles.h"
#include "imgui/renderer/imgui_core.h"
#include "sdl_window.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_streamline.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "core/debug_state.h"
#include "apds/apds.h"
#include "ffx_vk_framegeneration_presenter.h"

namespace Vulkan {

static constexpr vk::SurfaceFormatKHR SURFACE_FORMAT_HDR = {
    .format = vk::Format::eA2B10G10R10UnormPack32,
    .colorSpace = vk::ColorSpaceKHR::eHdr10St2084EXT,
};

Swapchain::Swapchain(const Instance& instance_, const Frontend::WindowSDL& window_)
    : instance{instance_}, window{window_} {
    const char* headless_env = std::getenv("GOW3_HEADLESS");
    headless = headless_env && headless_env[0] == '1' && headless_env[1] == '\0';
    if (headless) {
        surface_format = {vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
        Gow3Dxgi::SetActiveApi(Gow3Dxgi::Api::Vulkan);
        std::printf("GPU: GOW3_HEADLESS=1: real offscreen rendering, no WSI surface or visible output\n");
    } else {
        const auto req_api = Gow3Dxgi::RequestedApi();
        const int fg_req = Gow3Settings::Get().frame_generation.load(std::memory_order_relaxed);
        auto* sl = Streamline::Get();
        dlss_g_active_ = (fg_req >= 2) && sl && sl->IsDlssGSupported();
        fsr_fg_active_ = (fg_req == 1) && instance.IsStorageImageWriteWithoutFormatEnabled();
        const bool fg_needs_vulkan = dlss_g_active_ || fsr_fg_active_;
        if (fg_needs_vulkan && req_api != Gow3Dxgi::Api::Vulkan) {
            std::printf("Swapchain: Frame Generation active — using native Vulkan WSI instead of DXGI proxy\n");
            std::fflush(stdout);
        }
        if (req_api != Gow3Dxgi::Api::Vulkan && !fg_needs_vulkan) {
            dxgi_bridge = std::make_unique<Gow3Dxgi::PresenterBridge>();
            if (dxgi_bridge->Initialize(instance, window.GetWindowInfo().render_surface,
                                        window.GetWidth(), window.GetHeight(), req_api)) {
                const auto active = dxgi_bridge->GetApi();
                surface_format = (active == Gow3Dxgi::Api::DirectX9)
                                     ? vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm,
                                                            vk::ColorSpaceKHR::eSrgbNonlinear}
                                     : vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm,
                                                            vk::ColorSpaceKHR::eSrgbNonlinear};
                use_dxgi = true;
                vsync = Gow3Settings::Get().vsync;
            } else {
                dxgi_bridge.reset();
                use_dxgi = false;
            }
        }
        if (!use_dxgi) {
            Gow3Dxgi::SetActiveApi(Gow3Dxgi::Api::Vulkan);
            surface = CreateSurface(instance.GetInstance(), window);
            FindPresentFormat();
            FindPresentMode();
        }
    }

    Create(window.GetWidth(), window.GetHeight());
    ImGui::Core::Initialize(instance, window, image_count, surface_format.format);
}

Swapchain::~Swapchain() {
    Destroy();
    if (surface) instance.GetInstance().destroySurfaceKHR(surface);
}

void Swapchain::Create(u32 width_, u32 height_) {
    const bool first_creation = !created;
    width = width_;
    height = height_;
    needs_recreation = false;

    Destroy();
    const auto mark_created = [&](const char* api) {
        created = true;
        present_dirty = false;
        ++generation;
        if (first_creation) {
            std::printf("Swapchain: created %ux%u via %s\n", extent.width, extent.height, api);
            std::fflush(stdout);
        }
    };
    if (dxgi_present_failed) {
        // Recreating the same broken presentation device every frame cannot repair it.
        // Only presentation changes: retain the user's saved preference for the next session.
        std::printf("GPU: DirectX presentation failed; switching this session to Vulkan WSI\n");
        dxgi_bridge.reset();
        use_dxgi = dxgi_initialized_images = dxgi_present_failed = false;
        Gow3Dxgi::SetActiveApi(Gow3Dxgi::Api::Vulkan);
        surface = CreateSurface(instance.GetInstance(), window);
        FindPresentFormat();
        FindPresentMode();
        ImGui::Core::OnSurfaceFormatChange(surface_format.format);
    }
    // gow3 1 Nov 2026: the semaphores and fences are new and the image count may have shrunk
    // (frame_index indexed past image_acquired after such a recreate).
    image_index = frame_index = 0;

    if (headless) {
        ASSERT_MSG(width && height, "Offscreen rendering requires nonzero dimensions");
        extent = {width, height};
        CreateOffscreenImages();
        mark_created("offscreen");
        return;
    }

    if (use_dxgi && dxgi_bridge) {
        extent = {std::max(width, 1u), std::max(height, 1u)};
        image_count = Gow3Dxgi::PresenterBridge::kBufferCount;
        const bool ok = dxgi_initialized_images
                            ? dxgi_bridge->Resize(extent.width, extent.height,
                                                  surface_format.format, images, images_view)
                            : dxgi_bridge->CreateSharedImages(
                                  extent.width, extent.height, surface_format.format, images,
                                  images_view);
        if (ok) {
            dxgi_initialized_images = true;
            mark_created("DXGI flip model");
            return;
        }
        std::printf("GPU: DXGI shared image creation failed; falling back to Vulkan WSI\n");
        dxgi_bridge->ReleaseSharedImages(images, images_view);
        dxgi_bridge.reset();
        use_dxgi = false;
        dxgi_initialized_images = false;
        Gow3Dxgi::SetActiveApi(Gow3Dxgi::Api::Vulkan);
        if (!surface) {
            surface = CreateSurface(instance.GetInstance(), window);
            FindPresentFormat();
            FindPresentMode();
        }
    }

    SetSurfaceProperties();

    const std::array queue_family_indices = {
        instance.GetGraphicsQueueFamilyIndex(),
        instance.GetPresentQueueFamilyIndex(),
    };

    const bool exclusive = queue_family_indices[0] == queue_family_indices[1];
    const u32 queue_family_indices_count = exclusive ? 1u : 2u;
    const vk::SharingMode sharing_mode =
        exclusive ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
    const auto format = needs_hdr ? SURFACE_FORMAT_HDR : surface_format;
    const vk::SwapchainCreateInfoKHR swapchain_info = {
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
        .imageSharingMode = sharing_mode,
        .queueFamilyIndexCount = queue_family_indices_count,
        .pQueueFamilyIndices = queue_family_indices.data(),
        .preTransform = transform,
        .compositeAlpha = composite_alpha,
        .presentMode = present_mode,
        .clipped = true,
        .oldSwapchain = nullptr,
    };

    auto [swapchain_result, chain] = instance.GetDevice().createSwapchainKHR(swapchain_info);
    ASSERT_MSG(swapchain_result == vk::Result::eSuccess, "Failed to create swapchain: {}",
               vk::to_string(swapchain_result));
    swapchain = chain;

    SetupImages();
    RefreshSemaphores();
    mark_created("Vulkan WSI");
}

void Swapchain::Recreate(u32 width_, u32 height_, const char* reason) {
    // A request that matches the live swapchain and no pending change cannot repair anything:
    // recreate only what actually changed. The old path rebuilt the swapchain on every frame
    // in which presentation reported a failure, which is the 410-recreations-in-5-s storm.
    const bool same_extent = created && width_ == width && height_ == height;
    if (same_extent && !needs_recreation && !present_dirty && !dxgi_present_failed) {
        ++redundant_recreations;
        if (redundant_recreations == 1 || redundant_recreations % 600 == 0) {
            std::printf("Swapchain: redundant recreation suppressed (%llu so far, %ux%u)\n",
                        static_cast<unsigned long long>(redundant_recreations), width_, height_);
            std::fflush(stdout);
        }
        return;
    }
    const char* why = reason ? reason
                      : dxgi_present_failed ? "DirectX presentation failed"
                      : needs_recreation ? "surface out of date/lost or presentation failed"
                      : same_extent ? "presentation mode or HDR changed"
                                    : "window resize";
    LOG_DEBUG(Render_Vulkan, "Recreate the swapchain: width={} height={} HDR={}", width_, height_,
              needs_hdr);
    // gow3: how often (a capture hook such as OBS's sets itself up per swapchain).
    static u32 recreations = 0;
    static auto last = std::chrono::steady_clock::now();
    ++recreations;
    if (const auto now = std::chrono::steady_clock::now(); now - last >= std::chrono::seconds(5)) {
        std::printf("Swapchain: %u recreations (5 s), now %ux%u (was %ux%u)\n", recreations, width_,
                    height_, width, height);
        std::fflush(stdout);
        recreations = 0;
        last = now;
    }
    std::printf("Swapchain: recreate #%llu (%s): %ux%u -> %ux%u\n",
                static_cast<unsigned long long>(generation + 1), why, width, height, width_, height_);
    std::fflush(stdout);
    Create(width_, height_);
}

void Swapchain::SetHDR(bool hdr) {
    if (headless || use_dxgi) return; // Diagnostic and DXGI RGBA8 flip targets use SDR RGBA8.
    if (needs_hdr == hdr) {
        return;
    }

    needs_hdr = hdr;
    present_dirty = true;
    Recreate(width, height, "HDR changed");
    ImGui::Core::OnSurfaceFormatChange(needs_hdr ? SURFACE_FORMAT_HDR.format
                                                 : surface_format.format);
}

static std::atomic<uint64_t> s_apds_frame_counter{0};

bool Swapchain::AcquireNextImage() {
    APDS::RecordFrameBegin(s_apds_frame_counter.load(std::memory_order_relaxed) + 1);
    if (headless) return true;
    if (use_dxgi) {
        ASSERT_MSG(dxgi_bridge->WaitForImage(image_index),
                   "DirectX has not finished reading shared image {}", image_index);
        return true;
    }
    vk::Device device = instance.GetDevice();
    vk::Result result;
    // gow3 1 Nov 2026 (user: Uncharted 1 jerky at 75 FPS on a 75 Hz monitor): the image is
    // waited for here, on the present thread, before the frame's copy into it is submitted.
    // Before, only the copy's semaphore waited for it: with the display's queue full the
    // graphics queue stalled behind that copy until the display let an image go, the game's
    // next frame of GPU work with it, and its end-of-pipe interrupts came two at once (the game
    // then ran two frames back to back and skipped a vblank). Waiting on the CPU keeps the
    // game's GPU work flowing, and the presentation waits it causes show up where the display
    // queue drain sees them (VideoOutDriver::PresentThread). GOW3_ACQUIRE_FENCE=0: semaphore only.
    static const bool fence_wait = [] {
        const char* env = std::getenv("GOW3_ACQUIRE_FENCE");
        return !(env && env[0] == '0');
    }();
    vk::Fence fence{};
    if (fence_wait && !dlss_g_active_ && frame_index < acquire_fences.size()) {
        // Only an acquire that took an image signals its fence (a failed one leaves it as it
        // was), so only such a fence is waited for before its reset. One still not signaled
        // after the wait is left to its signal and replaced (a reset or destroy would be invalid).
        if (acquire_armed[frame_index]) {
            if (device.waitForFences(acquire_fences[frame_index], VK_TRUE, 1'000'000'000ULL) ==
                vk::Result::eTimeout) {
                auto [create_result, created] = device.createFence({});
                if (create_result == vk::Result::eSuccess) {
                    acquire_fences[frame_index] = created;
                }
            }
            acquire_armed[frame_index] = 0;
        }
        fence = acquire_fences[frame_index];
        (void)device.resetFences(fence);
    }
    {
        Gow3Stats::WaitTimer timer{Gow3Stats::acquire_ns};
        result = device.acquireNextImageKHR(swapchain, std::numeric_limits<u64>::max(),
                                            image_acquired[frame_index], fence, &image_index);
        if (fence && (result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR)) {
            // Bounded: a display that never lets the image go must not stop presenting (the
            // copy's semaphore still orders the GPU side).
            const auto wait_start = std::chrono::steady_clock::now();
            acquire_armed[frame_index] =
                device.waitForFences(fence, VK_TRUE, 200'000'000ULL) != vk::Result::eSuccess;
            if (std::chrono::steady_clock::now() - wait_start > std::chrono::milliseconds(2)) {
                long_acquire_waits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    switch (result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorSurfaceLostKHR:
    case vk::Result::eErrorOutOfDateKHR:
    case vk::Result::eErrorUnknown:
        needs_recreation = true;
        break;
    default:
        LOG_CRITICAL(Render_Vulkan, "Swapchain acquire returned unknown result {}",
                     vk::to_string(result));
        UNREACHABLE();
        break;
    }

    return !needs_recreation;
}

bool Swapchain::Present() {
    auto& settings = Gow3Settings::Get();
    const int fps_cap = Gow3Graphics::PresentLimit(settings.fps_limit.load(std::memory_order_relaxed));
    const bool user_vsync = settings.vsync.load(std::memory_order_relaxed);
    // Unlimited FPS (fps_cap == 0) bypasses VSync throttling so the swapchain can present
    // immediately with tearing / mailbox instead of clamping to monitor refresh rate.
    const bool effective_vsync = (fps_cap == 0) ? false : user_vsync;
    const auto pacer_mode = static_cast<Gow3Dxgi::PacerMode>(
        std::clamp(settings.frame_pacer_mode.load(std::memory_order_relaxed), 0, 3));
    const double display_hz = Gow3DisplayRefreshRate();

    // No frame interpolation or Reflex SDK submission is connected here. Stored preferences
    // must not multiply the user's FPS cap or silently replace the real queue-depth setting.

    if (headless) {
        image_index = (image_index + 1) % image_count;
        frame_index = image_index;
        return true;
    }
    if (use_dxgi && dxgi_bridge) {
        bool ok = false;
        {
            Gow3Stats::WaitTimer timer{Gow3Stats::present_ns};
            ok = dxgi_bridge->Present(image_index, effective_vsync,
                                      frame_pacer, fps_cap, display_hz, pacer_mode);
        }
        image_index = (image_index + 1) % image_count;
        frame_index = image_index;
        if (!ok) {
            needs_recreation = true;
            dxgi_present_failed = true;
        }
        const uint32_t frame_draws = DebugState.GetDrawCallsAndReset();
        const uint32_t frame_dispatches = DebugState.GetDispatchesAndReset();
        APDS::RecordFramePresent(++s_apds_frame_counter, frame_draws, frame_dispatches);
        return !needs_recreation;
    }
    frame_pacer.Wait(fps_cap, effective_vsync, display_hz, pacer_mode);
    const vk::PresentInfoKHR present_info = {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &present_ready[image_index],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    vk::Result result;
    {
        Gow3Stats::WaitTimer timer{Gow3Stats::present_ns};
        result = instance.GetPresentQueue().presentKHR(present_info);
    }
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        needs_recreation = true;
    } else {
        ASSERT_MSG(result == vk::Result::eSuccess, "Swapchain presentation failed: {}",
                   vk::to_string(result));
    }

    frame_index = (frame_index + 1) % image_count;
    const uint32_t wsi_draws = DebugState.GetDrawCallsAndReset();
    const uint32_t wsi_dispatches = DebugState.GetDispatchesAndReset();
    APDS::RecordFramePresent(++s_apds_frame_counter, wsi_draws, wsi_dispatches);

    return !needs_recreation;
}

bool Swapchain::HasGpuPresentSync() const noexcept {
    return use_dxgi && dxgi_bridge && dxgi_bridge->HasGpuSync();
}

bool Swapchain::PrepareGpuPresentSync(vk::Semaphore& out_signal_sem,
                                      u64& out_signal_value) noexcept {
    if (!use_dxgi || !dxgi_bridge) {
        return false;
    }
    return dxgi_bridge->PrepareGpuPresentSync(out_signal_sem, out_signal_value);
}

void Swapchain::SetFrameGenActive(bool dlss_g_active, bool fsr_fg_active) {
    if (dlss_g_active == dlss_g_active_ && fsr_fg_active == fsr_fg_active_) {
        return;
    }
    const bool was_dlss_g = dlss_g_active_;
    const bool was_fsr_fg = fsr_fg_active_;
    dlss_g_active_ = dlss_g_active;
    fsr_fg_active_ = fsr_fg_active;
    if (headless) {
        return;
    }
    if ((dlss_g_active_ || fsr_fg_active_) && use_dxgi) {
        std::printf("Swapchain: switching from DXGI proxy to Vulkan WSI for Frame Generation\n");
        std::fflush(stdout);
        Scheduler::WaitDeviceIdle(instance);
        if (dxgi_bridge) {
            dxgi_bridge->ReleaseSharedImages(images, images_view);
            dxgi_bridge.reset();
        }
        use_dxgi = false;
        dxgi_initialized_images = false;
        Gow3Dxgi::SetActiveApi(Gow3Dxgi::Api::Vulkan);
        if (!surface) {
            surface = CreateSurface(instance.GetInstance(), window);
        }
        FindPresentFormat();
        FindPresentMode();
        Recreate(width, height, "Frame Generation enabled (Vulkan WSI)");
        ImGui::Core::OnSurfaceFormatChange(surface_format.format);
        return;
    }
    if (!use_dxgi && (was_dlss_g != dlss_g_active_ || was_fsr_fg != fsr_fg_active_)) {
        FindPresentMode();
        Recreate(width, height, dlss_g_active_ || was_dlss_g ? "DLSS-G toggled" : "FSR3-FG toggled");
    }
}

VkResult Swapchain::AcquireNextImageRaw(VkSemaphore signal_semaphore, u32* out_image_index) {
    if (headless || use_dxgi || !swapchain) {
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    vk::Device device = instance.GetDevice();
    if (raw_acquire_fence_) {
        if (raw_acquire_armed_) {
            if (device.waitForFences(raw_acquire_fence_, VK_TRUE, 1'000'000'000ULL) ==
                vk::Result::eTimeout) {
                auto [cr, created] = device.createFence({});
                if (cr == vk::Result::eSuccess) {
                    raw_acquire_fence_ = created;
                }
            }
            raw_acquire_armed_ = false;
        }
        (void)device.resetFences(raw_acquire_fence_);
    }
    u32 idx = 0;
    const vk::Result result = device.acquireNextImageKHR(
        swapchain, std::numeric_limits<u64>::max(), vk::Semaphore(signal_semaphore),
        raw_acquire_fence_, &idx);
    if (result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
        if (out_image_index) {
            *out_image_index = idx;
        }
        image_index = idx;
        if (raw_acquire_fence_) {
            raw_acquire_armed_ =
                device.waitForFences(raw_acquire_fence_, VK_TRUE, 200'000'000ULL) !=
                vk::Result::eSuccess;
        }
    }
    if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorSurfaceLostKHR ||
        result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorUnknown) {
        needs_recreation = true;
    }
    return static_cast<VkResult>(result);
}

bool Swapchain::PresentImage(u32 img_idx, vk::Semaphore wait_semaphore, bool apply_pacer) {
    if (headless || use_dxgi || !swapchain) {
        return false;
    }
    if (apply_pacer) {
        auto& settings = Gow3Settings::Get();
        const int fps_cap = Gow3Graphics::PresentLimit(settings.fps_limit.load(std::memory_order_relaxed));
        const bool user_vsync = settings.vsync.load(std::memory_order_relaxed);
        const bool effective_vsync = (fps_cap == 0) ? false : user_vsync;
        const auto pacer_mode = static_cast<Gow3Dxgi::PacerMode>(
            std::clamp(settings.frame_pacer_mode.load(std::memory_order_relaxed), 0, 3));
        const double display_hz = Gow3DisplayRefreshRate();
        frame_pacer.Wait(fps_cap, effective_vsync, display_hz, pacer_mode);
    }
    const vk::Semaphore wait_sem = wait_semaphore ? wait_semaphore : present_ready[img_idx];
    const vk::PresentInfoKHR present_info = {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait_sem,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &img_idx,
    };
    vk::Result result;
    {
        Gow3Stats::WaitTimer timer{Gow3Stats::present_ns};
        result = instance.GetPresentQueue().presentKHR(present_info);
    }
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        needs_recreation = true;
    } else {
        ASSERT_MSG(result == vk::Result::eSuccess, "Swapchain presentation failed: {}",
                   vk::to_string(result));
    }
    image_index = img_idx;
    if (apply_pacer) {
        frame_index = (frame_index + 1) % image_count;
        const uint32_t wsi_draws = DebugState.GetDrawCallsAndReset();
        const uint32_t wsi_dispatches = DebugState.GetDispatchesAndReset();
        APDS::RecordFramePresent(++s_apds_frame_counter, wsi_draws, wsi_dispatches);
    }
    return !needs_recreation;
}

void Swapchain::FindPresentFormat() {
    const auto [formats_result, formats] =
        instance.GetPhysicalDevice().getSurfaceFormatsKHR(surface);
    ASSERT_MSG(formats_result == vk::Result::eSuccess, "Failed to query surface formats: {}",
               vk::to_string(formats_result));

    // Check if the device supports HDR formats. Here we care of Rec.2020 PQ only as it is expected
    // game output. Other variants as e.g. linear Rec.2020 will require additional color space
    // rotation
    supports_hdr =
        std::find_if(formats.begin(), formats.end(), [](const vk::SurfaceFormatKHR& format) {
            return format == SURFACE_FORMAT_HDR;
        }) != formats.end();
    // Also make sure that user allowed us to use HDR
    supports_hdr &= EmulatorSettings.IsHdrAllowed();

    // If there is a single undefined surface format, the device doesn't care, so we'll just use
    // RGBA sRGB.
    if (formats[0].format == vk::Format::eUndefined) {
        surface_format.format = vk::Format::eR8G8B8A8Unorm;
        surface_format.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        return;
    }

    // Try to find a suitable format.
    for (const vk::SurfaceFormatKHR& sformat : formats) {
        vk::Format format = sformat.format;
        if (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eB8G8R8A8Unorm) {
            continue;
        }

        surface_format.format = format;
        surface_format.colorSpace = sformat.colorSpace;
        return;
    }

    UNREACHABLE_MSG("Unable to find required swapchain format!");
}

void Swapchain::FindPresentMode() {
    const auto [modes_result, modes] =
        instance.GetPhysicalDevice().getSurfacePresentModesKHR(surface);
    if (modes_result != vk::Result::eSuccess) {
        LOG_ERROR(Render, "Failed to query available present modes, falling back to Fifo as "
                          "guaranteed supported option.");
        present_mode = vk::PresentModeKHR::eFifo;
        return;
    }

    // gow3: VSync on is Fifo (no tearing, waits for the display); off is Mailbox when the
    // driver has it, else Immediate (tearing allowed), else Fifo. Unlimited FPS disables VSync.
    const auto& s = Gow3Settings::Get();
    const int fps_cap = Gow3Graphics::PresentLimit(s.fps_limit.load(std::memory_order_relaxed));
    vsync = (fps_cap == 0) ? false : s.vsync.load(std::memory_order_relaxed);
    if (dlss_g_active_ && vsync &&
        (!Streamline::Get() || !Streamline::Get()->IsVsyncSupportedWithDlssG())) {
        vsync = false; // Vulkan Streamline DLSS-G requires non-FIFO unless driver advertises support
    }
    const auto has = [&](vk::PresentModeKHR mode) { return std::ranges::find(modes, mode) != modes.cend(); };
    vk::PresentModeKHR mode =
        vsync ? vk::PresentModeKHR::eFifo
        : has(vk::PresentModeKHR::eMailbox) ? vk::PresentModeKHR::eMailbox
        : has(vk::PresentModeKHR::eImmediate) ? vk::PresentModeKHR::eImmediate
                                              : vk::PresentModeKHR::eFifo;
    if (fsr_fg_active_) {
        mode = static_cast<vk::PresentModeKHR>(ffxVkFrameGenerationSelectPresentMode(
            true, vsync, reinterpret_cast<const VkPresentModeKHR*>(modes.data()),
            static_cast<u32>(modes.size())));
    }
    present_dirty |= mode != present_mode;
    present_mode = mode;
    std::printf("Swapchain: VSync %s (%s)\n", vsync ? "on" : "off", vk::to_string(present_mode).c_str());
}

void Swapchain::UpdateVsync() {
    if (headless) return;
    const auto& s = Gow3Settings::Get();
    const int fps_cap = Gow3Graphics::PresentLimit(s.fps_limit.load(std::memory_order_relaxed));
    const bool dlss_g_blocks_vsync =
        dlss_g_active_ && (!Streamline::Get() || !Streamline::Get()->IsVsyncSupportedWithDlssG());
    const bool desired_vsync =
        (fps_cap == 0 || dlss_g_blocks_vsync) ? false : s.vsync.load(std::memory_order_relaxed);
    if (use_dxgi) {
        vsync = desired_vsync;
        return;
    }
    if (desired_vsync == vsync) {
        return;
    }
    FindPresentMode();
    Recreate(width, height, "VSync changed");
}

void Swapchain::SetSurfaceProperties() {
    const auto [capabilities_result, capabilities] =
        instance.GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface);
    if (capabilities_result != vk::Result::eSuccess) {
        // Capture the actual WSI failure boundary before aborting. This is
        // diagnostic only: no retry, window mutation, or fallback capability.
        const auto [support_result, supported] = instance.GetPhysicalDevice().getSurfaceSupportKHR(
            instance.GetPresentQueueFamilyIndex(), surface);
        std::fprintf(stderr, "WSI failure: capabilities=%s present_support=%s/%u queue=%u\n",
                     vk::to_string(capabilities_result).c_str(),
                     vk::to_string(support_result).c_str(), static_cast<unsigned>(supported),
                     instance.GetPresentQueueFamilyIndex());
#ifdef _WIN32
        const auto hwnd = static_cast<HWND>(window.GetWindowInfo().render_surface);
        DWORD owner_pid = 0;
        const DWORD owner_tid = GetWindowThreadProcessId(hwnd, &owner_pid);
        RECT client{}, outer{};
        const BOOL client_ok = GetClientRect(hwnd, &client);
        const BOOL outer_ok = GetWindowRect(hwnd, &outer);
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        const BOOL monitor_ok = GetMonitorInfoW(monitor, &monitor_info);
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        GetStartupInfoW(&startup_info);
        std::fprintf(stderr,
                     "WSI Win32: hwnd=%p valid=%d visible=%d iconic=%d owner=%lu/%lu caller=%lu/%lu "
                     "client=%d:%ld,%ld,%ld,%ld outer=%d:%ld,%ld,%ld,%ld "
                     "monitor=%p valid=%d remote=%d style=%llx exstyle=%llx startup_flags=%lx startup_show=%u\n",
                     static_cast<void*>(hwnd), IsWindow(hwnd), IsWindowVisible(hwnd), IsIconic(hwnd),
                     owner_pid, owner_tid, GetCurrentProcessId(), GetCurrentThreadId(),
                     client_ok, client.left, client.top, client.right, client.bottom,
                     outer_ok, outer.left, outer.top, outer.right, outer.bottom,
                     static_cast<void*>(monitor), monitor_ok, GetSystemMetrics(SM_REMOTESESSION),
                     static_cast<unsigned long long>(GetWindowLongPtrW(hwnd, GWL_STYLE)),
                     static_cast<unsigned long long>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)),
                     startup_info.dwFlags, static_cast<unsigned>(startup_info.wShowWindow));
#endif
        std::fflush(stderr);
    }
    ASSERT_MSG(capabilities_result == vk::Result::eSuccess,
               "Failed to query surface capabilities: {}", vk::to_string(capabilities_result));

    extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == std::numeric_limits<u32>::max()) {
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, width));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, height));
    }

    // Select number of images in swap chain, we prefer one buffer in the background to work on
    if (fsr_fg_active_) {
        image_count = ffxVkFrameGenerationRequestedImageCount(capabilities.minImageCount,
                                                              capabilities.maxImageCount, true);
    } else if (dlss_g_active_) {
        image_count = std::max(capabilities.minImageCount + 1, 3u);
        if (capabilities.maxImageCount > 0) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }
    } else {
        image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }
    }

    // Prefer identity transform if possible
    transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    if (!(capabilities.supportedTransforms & transform)) {
        transform = capabilities.currentTransform;
    }

    // Opaque is not supported everywhere.
    composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    if (!(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) {
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
    }
}

void Swapchain::Destroy() {
    vk::Device device = instance.GetDevice();
    const auto wait_result = Scheduler::WaitDeviceIdle(instance);
    ASSERT_MSG(wait_result == vk::Result::eSuccess,
               "Cannot destroy swapchain resources before device idle: {}",
               vk::to_string(wait_result));

    if (use_dxgi && dxgi_bridge) {
        dxgi_bridge->ReleaseSharedImages(images, images_view);
    }

    for (auto& image_view : images_view) {
        device.destroyImageView(image_view);
    }
    images_view.clear();

    for (size_t i = 0; i < offscreen_allocations.size(); ++i) {
        vmaDestroyImage(instance.GetAllocator(), images[i], offscreen_allocations[i]);
    }
    offscreen_allocations.clear();
    images.clear();

    if (swapchain) {
        device.destroySwapchainKHR(swapchain);
        swapchain = nullptr;
    }

    for (const auto& sem : image_acquired) {
        device.destroySemaphore(sem);
    }
    for (const auto& sem : present_ready) {
        device.destroySemaphore(sem);
    }

    image_acquired.clear();
    present_ready.clear();

    for (size_t i = 0; i < acquire_fences.size(); ++i) {
        // The presentation engine signals them, not a queue: waitIdle does not cover them. A
        // fence whose signal still has not come is left alone (destroying it would be invalid).
        if (i < acquire_armed.size() && acquire_armed[i] &&
            device.waitForFences(acquire_fences[i], VK_TRUE, 100'000'000ULL) != vk::Result::eSuccess) {
            continue;
        }
        device.destroyFence(acquire_fences[i]);
    }
    acquire_fences.clear();
    acquire_armed.clear();

    if (raw_acquire_fence_) {
        if (!raw_acquire_armed_ ||
            device.waitForFences(raw_acquire_fence_, VK_TRUE, 100'000'000ULL) == vk::Result::eSuccess) {
            device.destroyFence(raw_acquire_fence_);
        }
        raw_acquire_fence_ = nullptr;
        raw_acquire_armed_ = false;
    }
}

void Swapchain::CreateOffscreenImages() {
    const auto device = instance.GetDevice();
    image_count = 3;
    image_index = frame_index = 0;
    images.resize(image_count);
    images_view.resize(image_count);
    offscreen_allocations.resize(image_count);
    const VkImageCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    for (u32 i = 0; i < image_count; ++i) {
        VkImage image{};
        const auto result = vmaCreateImage(instance.GetAllocator(), &ci, &allocation_info,
                                           &image, &offscreen_allocations[i], nullptr);
        ASSERT_MSG(result == VK_SUCCESS, "Failed to allocate offscreen image: {}", int(result));
        images[i] = image;
        const auto [view_result, view] = device.createImageView(vk::ImageViewCreateInfo{
            .image = images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = surface_format.format,
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .levelCount = 1, .layerCount = 1},
        });
        ASSERT_MSG(view_result == vk::Result::eSuccess, "Failed to create offscreen view: {}",
                   vk::to_string(view_result));
        images_view[i] = view;
        SetObjectName(device, images[i], "Headless diagnostic image {}", i);
    }
}

void Swapchain::RefreshSemaphores() {
    const vk::Device device = instance.GetDevice();
    image_acquired.resize(image_count);
    present_ready.resize(image_count);

    for (vk::Semaphore& semaphore : image_acquired) {
        auto [semaphore_result, sem] = device.createSemaphore({});
        ASSERT_MSG(semaphore_result == vk::Result::eSuccess,
                   "Failed to create image acquired semaphore: {}",
                   vk::to_string(semaphore_result));
        semaphore = sem;
    }
    for (vk::Semaphore& semaphore : present_ready) {
        auto [semaphore_result, sem] = device.createSemaphore({});
        ASSERT_MSG(semaphore_result == vk::Result::eSuccess,
                   "Failed to create present ready semaphore: {}", vk::to_string(semaphore_result));
        semaphore = sem;
    }

    acquire_fences.resize(image_count);
    acquire_armed.assign(image_count, 0);
    for (vk::Fence& fence : acquire_fences) {
        auto [fence_result, created] = device.createFence({});
        ASSERT_MSG(fence_result == vk::Result::eSuccess, "Failed to create image acquired fence: {}",
                   vk::to_string(fence_result));
        fence = created;
    }

    for (u32 i = 0; i < image_count; ++i) {
        SetObjectName(device, image_acquired[i], "Swapchain Semaphore: image_acquired {}", i);
        SetObjectName(device, present_ready[i], "Swapchain Semaphore: present_ready {}", i);
    }

    auto [raw_fence_res, raw_fence] = device.createFence({});
    ASSERT_MSG(raw_fence_res == vk::Result::eSuccess, "Failed to create raw acquire fence: {}",
               vk::to_string(raw_fence_res));
    raw_acquire_fence_ = raw_fence;
    raw_acquire_armed_ = false;
}

void Swapchain::SetupImages() {
    vk::Device device = instance.GetDevice();
    auto [images_result, imgs] = device.getSwapchainImagesKHR(swapchain);
    ASSERT_MSG(images_result == vk::Result::eSuccess, "Failed to create swapchain images: {}",
               vk::to_string(images_result));
    images = std::move(imgs);
    image_count = static_cast<u32>(images.size());
    images_view.resize(image_count);
    for (u32 i = 0; i < image_count; ++i) {
        if (images_view[i]) {
            device.destroyImageView(images_view[i]);
        }
        auto [im_view_result, im_view] = device.createImageView(vk::ImageViewCreateInfo{
            .image = images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = needs_hdr ? SURFACE_FORMAT_HDR.format : surface_format.format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        });
        ASSERT_MSG(im_view_result == vk::Result::eSuccess, "Failed to create image view: {}",
                   vk::to_string(im_view_result));
        images_view[i] = im_view;
    }

    for (u32 i = 0; i < image_count; ++i) {
        SetObjectName(device, images[i], "Swapchain Image {}", i);
        SetObjectName(device, images_view[i], "Swapchain ImageView {}", i);
    }
}

} // namespace Vulkan
