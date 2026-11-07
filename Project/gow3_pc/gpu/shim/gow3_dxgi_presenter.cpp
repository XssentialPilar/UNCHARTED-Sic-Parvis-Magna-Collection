// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection
// Hardware DXGI Flip-Model Presentation Bridge (DirectX 11 / DirectX 12) + Deterministic Sub-ms
// Hybrid Frame Pacer (QPC + High-Resolution Waitable Timer + _mm_pause).

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <dxgi1_5.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <d3d9.h>
#include <wrl/client.h>
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif
#endif

#include "gow3_dxgi_presenter.h"
#include "gow3_settings.h"
#include "video_core/renderer_vulkan/vk_instance.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace Gow3Dxgi {

namespace {

std::atomic<int> g_active_api{static_cast<int>(Api::Vulkan)};

std::string LowerAscii(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const char ch : input) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

inline void CpuPause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#endif
}

} // namespace

Api ParseApi(std::string_view text) noexcept {
    const std::string norm = LowerAscii(text);
    if (norm == "dx11" || norm == "d3d11" || norm == "directx11" || norm == "1") {
        return Api::DirectX11;
    }
    if (norm == "dx12" || norm == "d3d12" || norm == "directx12" || norm == "2") {
        return Api::DirectX12;
    }
    return Api::Vulkan;
}

const char* ApiConfigName(Api api) noexcept {
    switch (api) {
    case Api::DirectX11:
        return "dx11";
    case Api::DirectX12:
        return "dx12";
    default:
        return "vulkan";
    }
}

const char* ApiOverlayTag(Api api) noexcept {
    switch (api) {
    case Api::DirectX11:
        return "D3D11";
    case Api::DirectX12:
        return "D3D12";
    default:
        return "VULKAN";
    }
}

PacerMode ParsePacerMode(std::string_view text) noexcept {
    const std::string norm = LowerAscii(text);
    if (norm == "timer" || norm == "sleep") {
        return PacerMode::Timer;
    }
    if (norm == "spin") {
        return PacerMode::Spin;
    }
    if (norm == "off" || norm == "none" || norm == "0") {
        return PacerMode::Off;
    }
    return PacerMode::Hybrid;
}

const char* PacerModeName(PacerMode mode) noexcept {
    switch (mode) {
    case PacerMode::Timer:
        return "timer";
    case PacerMode::Spin:
        return "spin";
    case PacerMode::Off:
        return "off";
    default:
        return "hybrid";
    }
}

Api RequestedApi() noexcept {
    if (const char* env = std::getenv("GOW3_GRAPHICS_API"); env && env[0]) {
        return ParseApi(env);
    }
    return static_cast<Api>(
        std::clamp(Gow3Settings::Get().graphics_api.load(std::memory_order_relaxed), 0, 2));
}

Api ActiveApi() noexcept {
    return static_cast<Api>(g_active_api.load(std::memory_order_relaxed));
}

void SetActiveApi(Api api) noexcept {
    g_active_api.store(static_cast<int>(api), std::memory_order_relaxed);
}

#if defined(_WIN32) && !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

FramePacer::FramePacer() noexcept {
#ifdef _WIN32
    LARGE_INTEGER freq{};
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
        qpc_freq = freq.QuadPart;
    }
    waitable_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                            TIMER_ALL_ACCESS);
    if (!waitable_timer) {
        waitable_timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    }
#endif
}

FramePacer::~FramePacer() noexcept {
#ifdef _WIN32
    if (waitable_timer) {
        CloseHandle(static_cast<HANDLE>(waitable_timer));
        waitable_timer = nullptr;
    }
#endif
}

void FramePacer::Reset() noexcept {
    next_deadline_ticks = 0.0;
    last_target_fps = 0;
}

void FramePacer::Wait(int target_fps, bool vsync, double display_hz, PacerMode mode) noexcept {
#ifdef _WIN32
    if (mode == PacerMode::Off || target_fps <= 0 || qpc_freq <= 0) {
        return;
    }
    // Always enforce target_fps with sub-millisecond precision when target_fps > 0,
    // protecting against unlocked tear-free refresh rate mismatches (e.g. 120 Hz / 144 Hz displays).

    if (target_fps != last_target_fps) {
        last_target_fps = target_fps;
        next_deadline_ticks = 0.0;
    }

    const double period_ticks = static_cast<double>(qpc_freq) / static_cast<double>(target_fps);
    LARGE_INTEGER now_li{};
    QueryPerformanceCounter(&now_li);
    const double now_ticks = static_cast<double>(now_li.QuadPart);

    if (next_deadline_ticks <= 0.0) {
        next_deadline_ticks = now_ticks + period_ticks;
        return;
    }

    const int64_t deadline_qpc = static_cast<int64_t>(std::llround(next_deadline_ticks));
    if (now_li.QuadPart < deadline_qpc) {
        // Hybrid mode sleeps on the high-resolution kernel waitable timer until 1.35 ms before
        // deadline, then finishes with _mm_pause() for sub-microsecond QPC accuracy (<0.45 ms dT).
        const int64_t spin_margin_ticks =
            mode == PacerMode::Spin  ? (deadline_qpc - now_li.QuadPart + 1)
            : mode == PacerMode::Timer ? (qpc_freq / 20000) // 50 us
                                       : (qpc_freq * 135) / 100000; // 1.35 ms

        const int64_t remaining = deadline_qpc - now_li.QuadPart;
        if (waitable_timer && remaining > spin_margin_ticks) {
            const int64_t sleep_ticks = remaining - spin_margin_ticks;
            const int64_t sleep_100ns = (sleep_ticks * 10'000'000LL) / qpc_freq;
            if (sleep_100ns > 500) {
                LARGE_INTEGER due{};
                due.QuadPart = -sleep_100ns;
                if (SetWaitableTimer(static_cast<HANDLE>(waitable_timer), &due, 0, nullptr, nullptr,
                                     FALSE)) {
                    WaitForSingleObject(static_cast<HANDLE>(waitable_timer), 50);
                }
            }
        }

        if (mode != PacerMode::Timer) {
            LARGE_INTEGER cur{};
            for (;;) {
                QueryPerformanceCounter(&cur);
                if (cur.QuadPart >= deadline_qpc) {
                    break;
                }
                CpuPause();
            }
        }
    }

    LARGE_INTEGER after_li{};
    QueryPerformanceCounter(&after_li);
    const double after_ticks = static_cast<double>(after_li.QuadPart);
    next_deadline_ticks += period_ticks;
    // Prevent burst catch-up after level loads or heavy stalls (> 0.5 frame behind schedule).
    if (after_ticks > next_deadline_ticks + period_ticks * 0.5) {
        next_deadline_ticks = after_ticks + period_ticks;
    }
#else
    (void)target_fps;
    (void)vsync;
    (void)display_hz;
    (void)mode;
#endif
}

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

struct PresenterBridge::Impl {
    using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    using D3D11CreateDeviceFn = PFN_D3D11_CREATE_DEVICE;
    using D3D12CreateDeviceFn = PFN_D3D12_CREATE_DEVICE;

    HMODULE dxgi_module = nullptr;
    HMODULE d3d11_module = nullptr;
    HMODULE d3d12_module = nullptr;
    HMODULE d3d9_module = nullptr;

    ComPtr<IDXGIFactory1> factory1;
    ComPtr<IDXGIFactory2> factory2;
    ComPtr<IDXGIAdapter1> adapter1;
    ComPtr<IDXGIAdapter3> adapter3;
    ComPtr<IDXGISwapChain> swapchain;
    ComPtr<IDXGISwapChain1> swapchain1;
    ComPtr<IDXGISwapChain3> swapchain3;

    // DirectX 9 state
    ComPtr<IDirect3D9Ex> d3d9ex;
    ComPtr<IDirect3DDevice9Ex> d3d9_device;
    std::array<ComPtr<IDirect3DTexture9>, kBufferCount> d3d9_shared_tex{};
    std::array<ComPtr<ID3D11Texture2D>, kBufferCount> d3d9_interop_d3d11_tex{};
    ComPtr<IDirect3DSurface9> d3d9_backbuffer;

    // DirectX 11 state
    ComPtr<ID3D11Device> d3d11_device;
    ComPtr<ID3D11DeviceContext> d3d11_context;
    std::array<ComPtr<ID3D11Texture2D>, kBufferCount> d3d11_shared_tex{};
    ComPtr<ID3D11Texture2D> d3d11_headless_target;

    // DirectX 12 state
    ComPtr<ID3D12Device> d3d12_device;
    ComPtr<ID3D12CommandQueue> d3d12_queue;
    std::array<ComPtr<ID3D12CommandAllocator>, kBufferCount> d3d12_cmd_alloc{};
    ComPtr<ID3D12GraphicsCommandList> d3d12_cmd_list;
    ComPtr<ID3D12Fence> d3d12_fence;
    HANDLE d3d12_fence_event = nullptr;
    uint64_t d3d12_fence_value = 0;
    std::array<uint64_t, kBufferCount> d3d12_frame_fence_values{};
    std::array<ComPtr<ID3D12Resource>, kBufferCount> d3d12_backbuffers{};
    std::array<ComPtr<ID3D12Resource>, kBufferCount> d3d12_shared_tex{};
    ComPtr<ID3D12Resource> d3d12_headless_target;

    // Shared NT handles + imported Vulkan device memory
    std::array<HANDLE, kBufferCount> shared_nt_handles{};
    std::array<VkDeviceMemory, kBufferCount> imported_vk_memories{};

    HWND hwnd = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    UINT swapchain_flags = 0;
    bool allow_tearing = false;
    bool headless_test = false;

    ~Impl() {
        WaitD3D12Idle();
        CloseSharedHandles();
        if (d3d12_fence_event) {
            CloseHandle(d3d12_fence_event);
            d3d12_fence_event = nullptr;
        }
    }

    void CloseSharedHandles() noexcept {
        for (HANDLE& h : shared_nt_handles) {
            if (h) {
                CloseHandle(h);
                h = nullptr;
            }
        }
    }

    void WaitD3D12Idle() noexcept {
        if (!d3d12_queue || !d3d12_fence || !d3d12_fence_event) {
            return;
        }
        const uint64_t val = ++d3d12_fence_value;
        if (SUCCEEDED(d3d12_queue->Signal(d3d12_fence.Get(), val))) {
            if (d3d12_fence->GetCompletedValue() < val) {
                if (SUCCEEDED(d3d12_fence->SetEventOnCompletion(val, d3d12_fence_event))) {
                    WaitForSingleObject(d3d12_fence_event, 2000);
                }
            }
        }
    }

    void WaitD3D12Frame(uint32_t idx) noexcept {
        if (!d3d12_fence || !d3d12_fence_event) {
            return;
        }
        const uint64_t target = d3d12_frame_fence_values[idx];
        if (target != 0 && d3d12_fence->GetCompletedValue() < target) {
            if (SUCCEEDED(d3d12_fence->SetEventOnCompletion(target, d3d12_fence_event))) {
                WaitForSingleObject(d3d12_fence_event, 2000);
            }
        }
    }
};
#else
struct PresenterBridge::Impl {};
#endif

PresenterBridge::PresenterBridge() noexcept = default;

PresenterBridge::~PresenterBridge() noexcept {
    delete impl;
    impl = nullptr;
}

bool PresenterBridge::Initialize(const Vulkan::Instance& vk_instance, void* hwnd_ptr,
                                 uint32_t width, uint32_t height, Api requested_api) noexcept {
#ifdef _WIN32
    if (requested_api == Api::Vulkan) {
        return false;
    }
    if (!vk_instance.HasExternalMemoryWin32()) {
        std::printf("GPU: VK_KHR_external_memory_win32 unavailable; falling back to Vulkan WSI\n");
        return false;
    }

    delete impl;
    impl = new Impl{};
    instance = &vk_instance;
    impl->hwnd = static_cast<HWND>(hwnd_ptr);
    impl->headless_test = (impl->hwnd == nullptr);
    impl->width = std::max(width, 1u);
    impl->height = std::max(height, 1u);

    impl->dxgi_module = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl->dxgi_module) {
        std::printf("GPU: dxgi.dll unavailable; falling back to Vulkan WSI\n");
        return false;
    }
    const auto create_factory = reinterpret_cast<Impl::CreateDXGIFactory1Fn>(
        GetProcAddress(impl->dxgi_module, "CreateDXGIFactory1"));
    if (!create_factory ||
        FAILED(create_factory(__uuidof(IDXGIFactory1),
                              reinterpret_cast<void**>(impl->factory1.GetAddressOf())))) {
        std::printf("GPU: CreateDXGIFactory1 failed; falling back to Vulkan WSI\n");
        return false;
    }
    (void)impl->factory1.As(&impl->factory2);

    ComPtr<IDXGIFactory5> factory5;
    if (impl->factory2 && SUCCEEDED(impl->factory2.As(&factory5))) {
        BOOL tearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing,
                                                    sizeof(tearing)))) {
            impl->allow_tearing = (tearing == TRUE);
        }
    }
    impl->swapchain_flags = impl->allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

    // Match the exact DXGI hardware adapter to Vulkan's physical device via LUID.
    uint8_t vk_luid[8]{};
    const bool has_luid = vk_instance.GetDeviceLuid(vk_luid);
    for (UINT idx = 0;; ++idx) {
        ComPtr<IDXGIAdapter1> candidate;
        if (impl->factory1->EnumAdapters1(idx, candidate.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc))) {
            continue;
        }
        if (has_luid) {
            if (std::memcmp(&desc.AdapterLuid, vk_luid, 8) == 0) {
                impl->adapter1 = candidate;
                break;
            }
        } else if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && !impl->adapter1) {
            impl->adapter1 = candidate;
        }
    }
    if (!impl->adapter1) {
        std::printf("GPU: Could not match DXGI adapter LUID; falling back to Vulkan WSI\n");
        return false;
    }
    (void)impl->adapter1.As(&impl->adapter3);

    const DXGI_SWAP_CHAIN_DESC1 sc_desc = {
        .Width = impl->width,
        .Height = impl->height,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = kBufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = impl->swapchain_flags,
    };

    if (requested_api == Api::DirectX11) {
        impl->d3d11_module = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!impl->d3d11_module) {
            std::printf("GPU: d3d11.dll unavailable; falling back to Vulkan WSI\n");
            return false;
        }
        const auto create_d3d11 = reinterpret_cast<Impl::D3D11CreateDeviceFn>(
            GetProcAddress(impl->d3d11_module, "D3D11CreateDevice"));
        if (!create_d3d11) {
            return false;
        }
        static constexpr D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_11_0;
        // Note: MSDN requires D3D_DRIVER_TYPE_UNKNOWN whenever pAdapter != nullptr.
        HRESULT hr = create_d3d11(impl->adapter1.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                  D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
                                  impl->d3d11_device.GetAddressOf(), &achieved,
                                  impl->d3d11_context.GetAddressOf());
        if (FAILED(hr)) {
            hr = create_d3d11(impl->adapter1.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                              D3D11_CREATE_DEVICE_BGRA_SUPPORT, &levels[1], 1, D3D11_SDK_VERSION,
                              impl->d3d11_device.GetAddressOf(), &achieved,
                              impl->d3d11_context.GetAddressOf());
        }
        if (FAILED(hr) || !impl->d3d11_device || !impl->d3d11_context) {
            std::printf("GPU: D3D11CreateDevice failed (hr=0x%08lx); falling back to Vulkan WSI\n",
                        static_cast<unsigned long>(hr));
            return false;
        }

        if (!impl->headless_test) {
            bool sc_created = false;
            if (impl->factory2) {
                hr = impl->factory2->CreateSwapChainForHwnd(impl->d3d11_device.Get(), impl->hwnd,
                                                            &sc_desc, nullptr, nullptr,
                                                            impl->swapchain1.GetAddressOf());
                if (FAILED(hr)) {
                    DXGI_SWAP_CHAIN_DESC1 sc_desc_seq = sc_desc;
                    sc_desc_seq.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                    hr = impl->factory2->CreateSwapChainForHwnd(impl->d3d11_device.Get(), impl->hwnd,
                                                                &sc_desc_seq, nullptr, nullptr,
                                                                impl->swapchain1.GetAddressOf());
                }
                if (SUCCEEDED(hr) && impl->swapchain1) {
                    (void)impl->swapchain1.As(&impl->swapchain);
                    sc_created = true;
                }
            }
            if (!sc_created) {
                DXGI_SWAP_CHAIN_DESC legacy_desc{};
                legacy_desc.BufferDesc.Width = impl->width;
                legacy_desc.BufferDesc.Height = impl->height;
                legacy_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                legacy_desc.BufferDesc.RefreshRate.Numerator = 0;
                legacy_desc.BufferDesc.RefreshRate.Denominator = 1;
                legacy_desc.SampleDesc.Count = 1;
                legacy_desc.SampleDesc.Quality = 0;
                legacy_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                legacy_desc.BufferCount = 1;
                legacy_desc.OutputWindow = impl->hwnd;
                legacy_desc.Windowed = TRUE;
                legacy_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                legacy_desc.Flags = 0;

                hr = impl->factory1->CreateSwapChain(impl->d3d11_device.Get(), &legacy_desc,
                                                     impl->swapchain.GetAddressOf());
                if (SUCCEEDED(hr) && impl->swapchain) {
                    (void)impl->swapchain.As(&impl->swapchain1);
                    sc_created = true;
                }
            }
            if (!sc_created || !impl->swapchain) {
                std::printf(
                    "GPU: D3D11 CreateSwapChain failed (hr=0x%08lx); falling back to Vulkan\n",
                    static_cast<unsigned long>(hr));
                return false;
            }
            (void)impl->factory1->MakeWindowAssociation(impl->hwnd, DXGI_MWA_NO_ALT_ENTER);
            ComPtr<IDXGIDevice1> dxgi_dev1;
            if (SUCCEEDED(impl->d3d11_device.As(&dxgi_dev1))) {
                (void)dxgi_dev1->SetMaximumFrameLatency(1);
            }
        }
        active_api = Api::DirectX11;
        initialized = true;
        zero_copy_interop = true;
        SetActiveApi(Api::DirectX11);
        std::printf("GPU: DirectX 11 hardware flip-discard presenter active (FL %u.%u, tearing=%d)\n",
                    (static_cast<unsigned>(achieved) >> 12) & 0xFu,
                    (static_cast<unsigned>(achieved) >> 8) & 0xFu, int(impl->allow_tearing));
        std::fflush(stdout);
        return true;
    }

    if (requested_api == Api::DirectX12) {
        impl->d3d12_module = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!impl->d3d12_module) {
            std::printf("GPU: d3d12.dll unavailable on this OS; trying DirectX 11 fallback\n");
            return Initialize(vk_instance, hwnd_ptr, width, height, Api::DirectX11);
        }
        const auto create_d3d12 = reinterpret_cast<Impl::D3D12CreateDeviceFn>(
            GetProcAddress(impl->d3d12_module, "D3D12CreateDevice"));
        if (!create_d3d12) {
            return Initialize(vk_instance, hwnd_ptr, width, height, Api::DirectX11);
        }
        HRESULT hr = create_d3d12(impl->adapter1.Get(), D3D_FEATURE_LEVEL_11_0,
                                  __uuidof(ID3D12Device),
                                  reinterpret_cast<void**>(impl->d3d12_device.GetAddressOf()));
        if (FAILED(hr) || !impl->d3d12_device) {
            std::printf("GPU: D3D12CreateDevice failed (hr=0x%08lx); trying DirectX 11 fallback\n",
                        static_cast<unsigned long>(hr));
            return Initialize(vk_instance, hwnd_ptr, width, height, Api::DirectX11);
        }

        const D3D12_COMMAND_QUEUE_DESC q_desc = {
            .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
            .Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
            .NodeMask = 0,
        };
        if (FAILED(impl->d3d12_device->CreateCommandQueue(
                &q_desc, __uuidof(ID3D12CommandQueue),
                reinterpret_cast<void**>(impl->d3d12_queue.GetAddressOf())))) {
            return false;
        }

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            if (FAILED(impl->d3d12_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                    reinterpret_cast<void**>(impl->d3d12_cmd_alloc[i].GetAddressOf())))) {
                return false;
            }
        }
        if (FAILED(impl->d3d12_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, impl->d3d12_cmd_alloc[0].Get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void**>(impl->d3d12_cmd_list.GetAddressOf())))) {
            return false;
        }
        (void)impl->d3d12_cmd_list->Close();

        if (FAILED(impl->d3d12_device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void**>(impl->d3d12_fence.GetAddressOf())))) {
            return false;
        }
        impl->d3d12_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!impl->d3d12_fence_event) {
            return false;
        }

        if (!impl->headless_test) {
            if (!impl->factory2) {
                std::printf("GPU: IDXGIFactory2 required for D3D12; falling back to D3D11\n");
                return Initialize(vk_instance, hwnd_ptr, width, height, Api::DirectX11);
            }
            hr = impl->factory2->CreateSwapChainForHwnd(impl->d3d12_queue.Get(), impl->hwnd,
                                                        &sc_desc, nullptr, nullptr,
                                                        impl->swapchain1.GetAddressOf());
            if (FAILED(hr) || !impl->swapchain1 || FAILED(impl->swapchain1.As(&impl->swapchain3))) {
                std::printf(
                    "GPU: D3D12 CreateSwapChainForHwnd failed (hr=0x%08lx); falling back to Vulkan\n",
                    static_cast<unsigned long>(hr));
                return false;
            }
            (void)impl->swapchain1.As(&impl->swapchain);
            (void)impl->factory1->MakeWindowAssociation(impl->hwnd, DXGI_MWA_NO_ALT_ENTER);
            for (uint32_t i = 0; i < kBufferCount; ++i) {
                if (FAILED(impl->swapchain3->GetBuffer(
                        i, __uuidof(ID3D12Resource),
                        reinterpret_cast<void**>(impl->d3d12_backbuffers[i].GetAddressOf())))) {
                    return false;
                }
            }
        }

        active_api = Api::DirectX12;
        initialized = true;
        zero_copy_interop = true;
        SetActiveApi(Api::DirectX12);
        std::printf("GPU: DirectX 12 hardware flip-discard presenter active (tearing=%d)\n",
                    int(impl->allow_tearing));
        std::fflush(stdout);
        return true;
    }
#else
    (void)vk_instance;
    (void)hwnd_ptr;
    (void)width;
    (void)height;
    (void)requested_api;
#endif
    return false;
}

bool PresenterBridge::CreateSharedImages(uint32_t width, uint32_t height, vk::Format view_format,
                                         std::vector<vk::Image>& out_images,
                                         std::vector<vk::ImageView>& out_views) noexcept {
#ifdef _WIN32
    if (!initialized || !impl || !instance) {
        return false;
    }
    impl->width = std::max(width, 1u);
    impl->height = std::max(height, 1u);

    const vk::Device vk_dev = instance->GetDevice();
    const VkDevice raw_dev = static_cast<VkDevice>(vk_dev);
    const auto& disp = VULKAN_HPP_DEFAULT_DISPATCHER;
    const auto get_win32_props = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
        disp.vkGetDeviceProcAddr(raw_dev, "vkGetMemoryWin32HandlePropertiesKHR"));

    out_images.assign(kBufferCount, vk::Image{});
    out_views.assign(kBufferCount, vk::ImageView{});

    const VkExternalMemoryHandleTypeFlagBits ext_handle_type =
        active_api == Api::DirectX11 ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
                                     : VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

    if (impl->headless_test) {
        if (active_api == Api::DirectX11 || active_api == Api::DirectX9) {
            const D3D11_TEXTURE2D_DESC target_desc = {
                .Width = impl->width,
                .Height = impl->height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .SampleDesc = {1, 0},
                .Usage = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_RENDER_TARGET,
                .CPUAccessFlags = 0,
                .MiscFlags = 0,
            };
            if (FAILED(impl->d3d11_device->CreateTexture2D(
                    &target_desc, nullptr, impl->d3d11_headless_target.ReleaseAndGetAddressOf()))) {
                return false;
            }
        } else {
            const D3D12_HEAP_PROPERTIES heap_props = {
                .Type = D3D12_HEAP_TYPE_DEFAULT,
                .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                .CreationNodeMask = 1,
                .VisibleNodeMask = 1,
            };
            const D3D12_RESOURCE_DESC res_desc = {
                .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                .Alignment = 0,
                .Width = impl->width,
                .Height = impl->height,
                .DepthOrArraySize = 1,
                .MipLevels = 1,
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .SampleDesc = {1, 0},
                .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
                .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            };
            if (FAILED(impl->d3d12_device->CreateCommittedResource(
                    &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_PRESENT,
                    nullptr, __uuidof(ID3D12Resource),
                    reinterpret_cast<void**>(
                        impl->d3d12_headless_target.ReleaseAndGetAddressOf())))) {
                return false;
            }
        }
    }

    const auto mem_props = instance->GetPhysicalDevice().getMemoryProperties();

    for (uint32_t i = 0; i < kBufferCount; ++i) {
        HANDLE nt_handle = nullptr;
        if (active_api == Api::DirectX11 || active_api == Api::DirectX9) {
            const D3D11_TEXTURE2D_DESC tex_desc = {
                .Width = impl->width,
                .Height = impl->height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .SampleDesc = {1, 0},
                .Usage = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                .CPUAccessFlags = 0,
                .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
            };
            if (FAILED(impl->d3d11_device->CreateTexture2D(
                    &tex_desc, nullptr, impl->d3d11_shared_tex[i].ReleaseAndGetAddressOf()))) {
                ReleaseSharedImages(out_images, out_views);
                return false;
            }
            ComPtr<IDXGIResource1> dxgi_res1;
            if (FAILED(impl->d3d11_shared_tex[i].As(&dxgi_res1)) ||
                FAILED(dxgi_res1->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &nt_handle)) ||
                !nt_handle) {
                ReleaseSharedImages(out_images, out_views);
                return false;
            }

            if (active_api == Api::DirectX9 && impl->d3d9_device) {
                HANDLE d3d9_handle = nullptr;
                if (FAILED(impl->d3d9_device->CreateTexture(
                        impl->width, impl->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                        D3DPOOL_DEFAULT, impl->d3d9_shared_tex[i].ReleaseAndGetAddressOf(),
                        &d3d9_handle)) || !d3d9_handle) {
                    ReleaseSharedImages(out_images, out_views);
                    return false;
                }
                if (FAILED(impl->d3d11_device->OpenSharedResource(
                        d3d9_handle, __uuidof(ID3D11Texture2D),
                        reinterpret_cast<void**>(impl->d3d9_interop_d3d11_tex[i].ReleaseAndGetAddressOf())))) {
                    ReleaseSharedImages(out_images, out_views);
                    return false;
                }
            }
        } else {
            const D3D12_HEAP_PROPERTIES heap_props = {
                .Type = D3D12_HEAP_TYPE_DEFAULT,
                .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                .CreationNodeMask = 1,
                .VisibleNodeMask = 1,
            };
            const D3D12_RESOURCE_DESC res_desc = {
                .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                .Alignment = 0,
                .Width = impl->width,
                .Height = impl->height,
                .DepthOrArraySize = 1,
                .MipLevels = 1,
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .SampleDesc = {1, 0},
                .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
                .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            };
            if (FAILED(impl->d3d12_device->CreateCommittedResource(
                    &heap_props, D3D12_HEAP_FLAG_SHARED, &res_desc, D3D12_RESOURCE_STATE_COMMON,
                    nullptr, __uuidof(ID3D12Resource),
                    reinterpret_cast<void**>(impl->d3d12_shared_tex[i].ReleaseAndGetAddressOf())))) {
                ReleaseSharedImages(out_images, out_views);
                return false;
            }
            if (FAILED(impl->d3d12_device->CreateSharedHandle(impl->d3d12_shared_tex[i].Get(),
                                                              nullptr, GENERIC_ALL, nullptr,
                                                              &nt_handle)) ||
                !nt_handle) {
                ReleaseSharedImages(out_images, out_views);
                return false;
            }
        }
        impl->shared_nt_handles[i] = nt_handle;

        const VkExternalMemoryImageCreateInfo ext_img_ci = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = static_cast<VkExternalMemoryHandleTypeFlags>(ext_handle_type),
        };
        const VkImageCreateInfo img_ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = &ext_img_ci,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {impl->width, impl->height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkImage raw_img = VK_NULL_HANDLE;
        if (disp.vkCreateImage(raw_dev, &img_ci, nullptr, &raw_img) != VK_SUCCESS || !raw_img) {
            ReleaseSharedImages(out_images, out_views);
            return false;
        }
        out_images[i] = vk::Image{raw_img};

        VkMemoryRequirements mem_reqs{};
        disp.vkGetImageMemoryRequirements(raw_dev, raw_img, &mem_reqs);
        uint32_t allowed_bits = mem_reqs.memoryTypeBits;
        if (get_win32_props) {
            VkMemoryWin32HandlePropertiesKHR handle_props{
                .sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR,
                .pNext = nullptr,
                .memoryTypeBits = 0,
            };
            if (get_win32_props(raw_dev, ext_handle_type, nt_handle, &handle_props) == VK_SUCCESS &&
                handle_props.memoryTypeBits != 0) {
                allowed_bits &= handle_props.memoryTypeBits;
            }
        }

        uint32_t chosen_type = ~0u;
        for (uint32_t t = 0; t < mem_props.memoryTypeCount; ++t) {
            if ((allowed_bits & (1u << t)) &&
                (mem_props.memoryTypes[t].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                chosen_type = t;
                break;
            }
        }
        if (chosen_type == ~0u) {
            for (uint32_t t = 0; t < mem_props.memoryTypeCount; ++t) {
                if (allowed_bits & (1u << t)) {
                    chosen_type = t;
                    break;
                }
            }
        }
        if (chosen_type == ~0u) {
            ReleaseSharedImages(out_images, out_views);
            return false;
        }

        const VkMemoryDedicatedAllocateInfo ded_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext = nullptr,
            .image = raw_img,
            .buffer = VK_NULL_HANDLE,
        };
        const VkImportMemoryWin32HandleInfoKHR import_info = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
            .pNext = &ded_info,
            .handleType = ext_handle_type,
            .handle = nt_handle,
            .name = nullptr,
        };
        const VkMemoryAllocateInfo alloc_ci = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &import_info,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = chosen_type,
        };

        VkDeviceMemory raw_mem = VK_NULL_HANDLE;
        if (disp.vkAllocateMemory(raw_dev, &alloc_ci, nullptr, &raw_mem) != VK_SUCCESS ||
            !raw_mem) {
            ReleaseSharedImages(out_images, out_views);
            return false;
        }
        impl->imported_vk_memories[i] = raw_mem;

        if (disp.vkBindImageMemory(raw_dev, raw_img, raw_mem, 0) != VK_SUCCESS) {
            ReleaseSharedImages(out_images, out_views);
            return false;
        }

        auto [view_res, view] = vk_dev.createImageView(vk::ImageViewCreateInfo{
            .image = out_images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = view_format,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        });
        if (view_res != vk::Result::eSuccess) {
            ReleaseSharedImages(out_images, out_views);
            return false;
        }
        out_views[i] = view;
    }

    return true;
#else
    (void)width;
    (void)height;
    (void)view_format;
    (void)out_images;
    (void)out_views;
    return false;
#endif
}

void PresenterBridge::ReleaseSharedImages(std::vector<vk::Image>& images,
                                          std::vector<vk::ImageView>& views) noexcept {
#ifdef _WIN32
    if (!impl || !instance) {
        return;
    }
    impl->WaitD3D12Idle();
    const vk::Device vk_dev = instance->GetDevice();
    const VkDevice raw_dev = static_cast<VkDevice>(vk_dev);
    const auto& disp = VULKAN_HPP_DEFAULT_DISPATCHER;

    for (auto& v : views) {
        if (v) {
            vk_dev.destroyImageView(v);
        }
    }
    views.clear();

    for (auto& img : images) {
        if (img) {
            disp.vkDestroyImage(raw_dev, static_cast<VkImage>(img), nullptr);
        }
    }
    images.clear();

    for (VkDeviceMemory& mem : impl->imported_vk_memories) {
        if (mem) {
            disp.vkFreeMemory(raw_dev, mem, nullptr);
            mem = VK_NULL_HANDLE;
        }
    }

    for (auto& tex : impl->d3d9_shared_tex) {
        tex.Reset();
    }
    for (auto& tex : impl->d3d9_interop_d3d11_tex) {
        tex.Reset();
    }
    impl->d3d9_backbuffer.Reset();
    for (auto& tex : impl->d3d11_shared_tex) {
        tex.Reset();
    }
    for (auto& res : impl->d3d12_shared_tex) {
        res.Reset();
    }
    impl->d3d11_headless_target.Reset();
    impl->d3d12_headless_target.Reset();
    impl->CloseSharedHandles();
#else
    (void)images;
    (void)views;
#endif
}

bool PresenterBridge::Resize(uint32_t width, uint32_t height, vk::Format view_format,
                             std::vector<vk::Image>& out_images,
                             std::vector<vk::ImageView>& out_views) noexcept {
#ifdef _WIN32
    if (!initialized || !impl || width == 0 || height == 0) {
        return false;
    }
    ReleaseSharedImages(out_images, out_views);

    if (active_api == Api::DirectX9 && !impl->headless_test && impl->d3d9_device) {
        for (auto& tex : impl->d3d9_shared_tex) {
            tex.Reset();
        }
        for (auto& tex : impl->d3d9_interop_d3d11_tex) {
            tex.Reset();
        }
        impl->d3d9_backbuffer.Reset();

        D3DPRESENT_PARAMETERS d3dpp{};
        d3dpp.BackBufferWidth = width;
        d3dpp.BackBufferHeight = height;
        d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
        d3dpp.BackBufferCount = kBufferCount;
        d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
        d3dpp.hDeviceWindow = impl->hwnd;
        d3dpp.Windowed = TRUE;
        d3dpp.EnableAutoDepthStencil = FALSE;
        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        if (FAILED(impl->d3d9_device->ResetEx(&d3dpp, nullptr))) {
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            impl->d3d9_device->ResetEx(&d3dpp, nullptr);
        }
    }

    if (!impl->headless_test && impl->swapchain) {
        if (active_api == Api::DirectX12) {
            impl->WaitD3D12Idle();
            for (auto& bb : impl->d3d12_backbuffers) {
                bb.Reset();
            }
            impl->d3d12_frame_fence_values.fill(0);
        } else if (impl->d3d11_context) {
            impl->d3d11_context->ClearState();
            impl->d3d11_context->Flush();
        }

        const UINT buf_count = (impl->swapchain1) ? kBufferCount : 0;
        const HRESULT hr = impl->swapchain->ResizeBuffers(
            buf_count, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, impl->swapchain_flags);
        if (FAILED(hr)) {
            std::printf("GPU: DXGI ResizeBuffers(%ux%u) failed (hr=0x%08lx)\n", width, height,
                        static_cast<unsigned long>(hr));
            return false;
        }

        if (active_api == Api::DirectX12 && impl->swapchain3) {
            for (uint32_t i = 0; i < kBufferCount; ++i) {
                if (FAILED(impl->swapchain3->GetBuffer(
                        i, __uuidof(ID3D12Resource),
                        reinterpret_cast<void**>(impl->d3d12_backbuffers[i].GetAddressOf())))) {
                    return false;
                }
            }
        }
    }

    return CreateSharedImages(width, height, view_format, out_images, out_views);
#else
    (void)width;
    (void)height;
    (void)view_format;
    (void)out_images;
    (void)out_views;
    return false;
#endif
}

bool PresenterBridge::Present(uint32_t image_index, bool vsync, FramePacer& pacer, int target_fps,
                              double display_hz, PacerMode pacer_mode) noexcept {
#ifdef _WIN32
    if (!initialized || !impl || image_index >= kBufferCount) {
        return false;
    }
    const UINT sync_interval = vsync ? 1u : 0u;
    const UINT present_flags = (!vsync && impl->allow_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    if (active_api == Api::DirectX9) {
        ID3D11Texture2D* src = impl->d3d11_shared_tex[image_index].Get();
        ID3D11Texture2D* dst_interop = impl->d3d9_interop_d3d11_tex[image_index].Get();
        if (!src || !dst_interop || !impl->d3d9_device) {
            return false;
        }
        impl->d3d11_context->CopyResource(dst_interop, src);
        impl->d3d11_context->Flush();

        if (!impl->headless_test) {
            ComPtr<IDirect3DSurface9> backbuffer;
            HRESULT hr = impl->d3d9_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                                          backbuffer.GetAddressOf());
            if (SUCCEEDED(hr) && backbuffer) {
                ComPtr<IDirect3DSurface9> src_surface;
                if (SUCCEEDED(impl->d3d9_shared_tex[image_index]->GetSurfaceLevel(
                        0, src_surface.GetAddressOf())) && src_surface) {
                    impl->d3d9_device->StretchRect(src_surface.Get(), nullptr, backbuffer.Get(),
                                                   nullptr, D3DTEXF_NONE);
                }
            }
        }

        pacer.Wait(target_fps, vsync, display_hz, pacer_mode);
        HRESULT hr = S_OK;
        if (!impl->headless_test) {
            hr = impl->d3d9_device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
        }
        return SUCCEEDED(hr) || hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
    }

    if (active_api == Api::DirectX11) {
        ID3D11Texture2D* src = impl->d3d11_shared_tex[image_index].Get();
        if (!src) {
            return false;
        }
        if (impl->headless_test) {
            impl->d3d11_context->CopyResource(impl->d3d11_headless_target.Get(), src);
            impl->d3d11_context->Flush();
            pacer.Wait(target_fps, vsync, display_hz, pacer_mode);
            return true;
        }

        ComPtr<ID3D11Texture2D> backbuffer;
        if (FAILED(impl->swapchain->GetBuffer(
                0, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(backbuffer.GetAddressOf()))) ||
            !backbuffer) {
            return false;
        }
        impl->d3d11_context->CopyResource(backbuffer.Get(), src);
        pacer.Wait(target_fps, vsync, display_hz, pacer_mode);
        const HRESULT hr = impl->swapchain->Present(sync_interval, present_flags);
        return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
    }

    if (active_api == Api::DirectX12) {
        ID3D12Resource* src = impl->d3d12_shared_tex[image_index].Get();
        if (!src) {
            return false;
        }
        const uint32_t back_idx =
            impl->headless_test ? (image_index % kBufferCount)
                                : impl->swapchain3->GetCurrentBackBufferIndex();
        ID3D12Resource* dst = impl->headless_test ? impl->d3d12_headless_target.Get()
                                                  : impl->d3d12_backbuffers[back_idx].Get();
        if (!dst) {
            return false;
        }

        impl->WaitD3D12Frame(back_idx);
        if (FAILED(impl->d3d12_cmd_alloc[back_idx]->Reset()) ||
            FAILED(impl->d3d12_cmd_list->Reset(impl->d3d12_cmd_alloc[back_idx].Get(), nullptr))) {
            return false;
        }

        const D3D12_RESOURCE_BARRIER pre_barriers[2] = {
            {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition = {
                    .pResource = dst,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                    .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                },
            },
            {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition = {
                    .pResource = src,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                    .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
                },
            },
        };
        impl->d3d12_cmd_list->ResourceBarrier(2, pre_barriers);
        impl->d3d12_cmd_list->CopyResource(dst, src);

        const D3D12_RESOURCE_BARRIER post_barriers[2] = {
            {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition = {
                    .pResource = dst,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                    .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                },
            },
            {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition = {
                    .pResource = src,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE,
                    .StateAfter = D3D12_RESOURCE_STATE_COMMON,
                },
            },
        };
        impl->d3d12_cmd_list->ResourceBarrier(2, post_barriers);
        if (FAILED(impl->d3d12_cmd_list->Close())) {
            return false;
        }

        ID3D12CommandList* lists[] = {impl->d3d12_cmd_list.Get()};
        impl->d3d12_queue->ExecuteCommandLists(1, lists);

        pacer.Wait(target_fps, vsync, display_hz, pacer_mode);
        HRESULT hr = S_OK;
        if (!impl->headless_test) {
            hr = impl->swapchain->Present(sync_interval, present_flags);
        }

        const uint64_t fv = ++impl->d3d12_fence_value;
        (void)impl->d3d12_queue->Signal(impl->d3d12_fence.Get(), fv);
        impl->d3d12_frame_fence_values[back_idx] = fv;
        if (impl->headless_test) {
            impl->WaitD3D12Frame(back_idx);
        }
        return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
    }
#else
    (void)image_index;
    (void)vsync;
    (void)pacer;
    (void)target_fps;
    (void)display_hz;
    (void)pacer_mode;
#endif
    return false;
}

bool PresenterBridge::QueryLocalVram(uint64_t& out_usage_bytes,
                                     uint64_t& out_budget_bytes) const noexcept {
#ifdef _WIN32
    if (!impl || !impl->adapter3) {
        return false;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(impl->adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        return false;
    }
    out_usage_bytes = info.CurrentUsage;
    out_budget_bytes = info.Budget;
    return true;
#else
    (void)out_usage_bytes;
    (void)out_budget_bytes;
    return false;
#endif
}

} // namespace Gow3Dxgi
