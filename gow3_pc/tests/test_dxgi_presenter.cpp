// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection
// Hardware regression test for DirectX 11 & DirectX 12 zero-copy NT-handle Vulkan interop,
// hard VRAM budget enforcement, and sub-millisecond Hybrid QPC FramePacer accuracy.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "gow3_dxgi_presenter.h"
#include "video_core/renderer_vulkan/vk_instance.h"

// Keep Vulkan-Hpp dispatcher ABI consistent with gow3gpu.
#undef NDEBUG
#include <cassert>

// Exercise real Vulkan writes and ownership transfers, rather than presenting an
// uninitialized texture. Completion must be observed before reusing each ring slot.
static void RenderAndPresent(const Vulkan::Instance& instance, Gow3Dxgi::PresenterBridge& bridge,
                             const std::vector<vk::Image>& images, uint32_t frame,
                             bool vsync = false) {
    const uint32_t index = frame % Gow3Dxgi::PresenterBridge::kBufferCount;
    assert(bridge.WaitForImage(index));
    const auto device = instance.GetDevice();
    const auto [pool_result, pool] = device.createCommandPool({
        .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex()});
    assert(pool_result == vk::Result::eSuccess);
    const auto [cmd_result, cmds] = device.allocateCommandBuffers({
        .commandPool = pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    assert(cmd_result == vk::Result::eSuccess);
    const auto cmd = cmds[0];
    assert(cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}) == vk::Result::eSuccess);
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    const vk::ImageMemoryBarrier acquire{
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
        .dstQueueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
        .image = images[index], .subresourceRange = range};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
                        {}, {}, {}, acquire);
    const vk::ClearColorValue color{std::array<float, 4>{float(frame % 17) / 16, 0.25f, 0.75f, 1}};
    cmd.clearColorImage(images[index], vk::ImageLayout::eTransferDstOptimal, color, range);
    const vk::ImageMemoryBarrier release{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
        .image = images[index], .subresourceRange = range};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                        {}, {}, {}, release);
    assert(cmd.end() == vk::Result::eSuccess);
    const auto [fence_result, fence] = device.createFence({});
    assert(fence_result == vk::Result::eSuccess);
    assert(instance.GetGraphicsQueue().submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &cmd}, fence)
           == vk::Result::eSuccess);
    assert(device.waitForFences(fence, VK_TRUE, 2'000'000'000ULL) == vk::Result::eSuccess);
    Gow3Dxgi::FramePacer pacer;
    assert(bridge.Present(index, vsync, pacer, 120, 144.0, Gow3Dxgi::PacerMode::Off));
    device.destroyFence(fence);
    device.destroyCommandPool(pool);
}

int main() {
#ifdef _WIN32
    _putenv_s("GOW3_VRAM_BUDGET_MB", "820");
    _putenv_s("GOW3_PIPELINE_BINARIES", "0");

    // 1. Sub-millisecond Hybrid FramePacer accuracy check (60 FPS = 16.6667 ms).
    {
        Gow3Dxgi::FramePacer pacer;
        pacer.Wait(60, false, 144.0, Gow3Dxgi::PacerMode::Hybrid); // Arm baseline deadline
        constexpr int kFrames = 12;
        double max_err_ms = 0.0;
        auto prev = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            pacer.Wait(60, false, 144.0, Gow3Dxgi::PacerMode::Hybrid);
            const auto now = std::chrono::steady_clock::now();
            const double dt_ms =
                std::chrono::duration<double, std::milli>(now - prev).count();
            prev = now;
            if (i >= 1) {
                const double err = std::abs(dt_ms - (1000.0 / 60.0));
                if (err > max_err_ms) {
                    max_err_ms = err;
                }
            }
        }
        std::printf("[dxgi-presenter-test] Hybrid 60 FPS pacer max |dT - 16.67ms| = %.3f ms\n",
                    max_err_ms);
        assert(max_err_ms < 1.0);
    }

    // 2. Real hardware Vulkan 1.3 + VRAM hard cap (820 MiB) + D3D11/D3D12 zero-copy interop.
    Vulkan::Instance instance{-1, std::getenv("SPM_TEST_VALIDATION") != nullptr};
    const u64 cap_bytes = u64{820} << 20;
    std::printf("[dxgi-presenter-test] TotalMemoryBudget=%llu MiB, BudgetNow=%llu MiB, ExtMemWin32=%d\n",
                static_cast<unsigned long long>(instance.GetTotalMemoryBudget() >> 20),
                static_cast<unsigned long long>(instance.GetDeviceMemoryBudgetNow() >> 20),
                int(instance.HasExternalMemoryWin32()));
    assert(instance.GetTotalMemoryBudget() <= cap_bytes);
    assert(instance.GetDeviceMemoryBudgetNow() <= cap_bytes);
    assert(instance.HasExternalMemoryWin32());

    HWND test_hwnd = CreateWindowExW(0, L"STATIC", L"SPM_DXGI_Test", WS_POPUP, 0, 0, 640, 360,
                                     nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(test_hwnd != nullptr);

    for (const Gow3Dxgi::Api api : {Gow3Dxgi::Api::DirectX11, Gow3Dxgi::Api::DirectX12, Gow3Dxgi::Api::DirectX9}) {
        Gow3Dxgi::PresenterBridge bridge;
        const bool init_ok = bridge.Initialize(instance, test_hwnd, 640, 360, api);
        assert(init_ok);
        assert(bridge.IsInitialized());
        assert(bridge.UsesZeroCopyInterop());

        uint64_t vram_used = 0;
        uint64_t vram_budget = 0;
        assert(bridge.QueryLocalVram(vram_used, vram_budget));
        assert(vram_budget > 0);

        std::vector<vk::Image> images;
        std::vector<vk::ImageView> views;
        const auto req_fmt = (api == Gow3Dxgi::Api::DirectX9) ? vk::Format::eB8G8R8A8Unorm : vk::Format::eR8G8B8A8Unorm;
        assert(bridge.CreateSharedImages(640, 360, req_fmt, images, views));
        assert(images.size() == Gow3Dxgi::PresenterBridge::kBufferCount);
        assert(views.size() == Gow3Dxgi::PresenterBridge::kBufferCount);
        for (size_t i = 0; i < images.size(); ++i) {
            assert(static_cast<bool>(images[i]));
            assert(static_cast<bool>(views[i]));
        }

        for (uint32_t f = 0; f < 90; ++f) {
            RenderAndPresent(instance, bridge, images, f);
        }
        RenderAndPresent(instance, bridge, images, 90, true);

        for (const auto size : {vk::Extent2D{1280, 800}, vk::Extent2D{1920, 1200}, vk::Extent2D{1280, 720}}) {
            assert(bridge.Resize(size.width, size.height, req_fmt, images, views));
            for (uint32_t f = 0; f < 18; ++f) {
                RenderAndPresent(instance, bridge, images, f);
            }
        }

        bridge.ReleaseSharedImages(images, views);
        assert(images.empty() && views.empty());
        std::printf("[dxgi-presenter-test] %s Vulkan writes, ring reuse and 16:10/16:9 resize PASSED (initial VRAM=%llu/%llu MiB)\n",
                    Gow3Dxgi::ApiOverlayTag(bridge.GetApi()),
                    static_cast<unsigned long long>(vram_used >> 20),
                    static_cast<unsigned long long>(vram_budget >> 20));
    }
    DestroyWindow(test_hwnd);
#endif
    return 0;
}
