// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Core Types, Error Handling & 2015+ Hardware Tiers
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#else
using HRESULT = int32_t;
inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005L);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057L);
inline constexpr bool SUCCEEDED(HRESULT hr) noexcept { return hr >= 0; }
inline constexpr bool FAILED(HRESULT hr) noexcept { return hr < 0; }
#endif

namespace unc::core {

inline constexpr std::string_view kCollectionTitle = "Uncharted: Sic Parvis Magna Collection";
inline constexpr std::string_view kCollectionVersion = "1.0.0-SPM";

enum class GameTitleId : uint8_t {
    Uncharted1DrakesFortune = 1,
    Uncharted2AmongThieves = 2,
    Uncharted3DrakesDeception = 3,
};

enum class HardwareTier : uint8_t {
    Unsupported = 0,
    Entry2015_720p_1GB = 1,     // Q8400 / Phenom II X4 945 / i3-2100, 4 GB RAM, 1 GB VRAM (GTX 460/550 Ti/650, HD 5770/6790/7750)
    MidRange2015_1080p_2GB = 2, // i5-2400 / FX-6300, 6-8 GB RAM, 2 GB VRAM (GTX 750 Ti/660, HD 7850/R7 265/R9 270)
    ModernHigh_1440p_4GBPlus = 3,
};

enum class GraphicsApi : uint8_t {
    DirectX11_FL11_0 = 0,
    DirectX12 = 1,
    Vulkan13 = 2,
    DirectX9 = 3,
};

struct SystemSpecs {
    uint32_t cpu_logical_cores{4};
    uint64_t system_ram_mib{4096};
    uint64_t dedicated_vram_mib{1024};
    bool supports_dx11_fl11_0{true};
    bool supports_dx12{false};
    bool supports_vulkan_1_3{true};
    bool has_popcnt{true};
    bool has_avx{false};
};

struct RecommendedProfile {
    HardwareTier tier{HardwareTier::Entry2015_720p_1GB};
    GraphicsApi preferred_api{GraphicsApi::DirectX11_FL11_0};
    uint32_t internal_width{1280};
    uint32_t internal_height{720};
    uint32_t target_fps{30};
    uint64_t direct_memory_pool_mib{3584};
    uint64_t vram_gc_trigger_mib{563};
    uint64_t vram_gc_pressure_mib{716};
    uint64_t vram_gc_critical_mib{839};
    bool low_ram_mode{true};
    bool low_vram_mode{true};
    bool enable_isa_trap_emulation{true};
};

class HResultException final : public std::runtime_error {
public:
    HResultException(HRESULT hr, std::string_view expression,
                     const std::source_location& location = std::source_location::current())
        : std::runtime_error(FormatMessage(hr, expression, location)),
          hr_(hr),
          line_(location.line()),
          file_(location.file_name()) {}

    [[nodiscard]] HRESULT code() const noexcept { return hr_; }
    [[nodiscard]] uint32_t line() const noexcept { return line_; }
    [[nodiscard]] const char* file() const noexcept { return file_; }

private:
    static std::string FormatMessage(HRESULT hr, std::string_view expression,
                                     const std::source_location& loc) {
        char buf[512]{};
        std::snprintf(buf, sizeof(buf),
                      "[Uncharted: Sic Parvis Magna Collection] HRESULT 0x%08X failed in '%.*s' at %s:%u",
                      static_cast<uint32_t>(hr), static_cast<int>(expression.size()),
                      expression.data(), loc.file_name(), static_cast<unsigned>(loc.line()));
        return std::string(buf);
    }

    HRESULT hr_;
    uint32_t line_;
    const char* file_;
};

inline void ThrowIfFailed(HRESULT hr, std::string_view expression,
                          const std::source_location& loc = std::source_location::current()) {
    if (FAILED(hr)) {
        throw HResultException(hr, expression, loc);
    }
}

#define THROW_IF_FAILED(hr_expr) \
    ::unc::core::ThrowIfFailed((hr_expr), #hr_expr, std::source_location::current())

#define UNC_THROW_IF_FAILED(hr_expr) \
    ::unc::core::ThrowIfFailed((hr_expr), #hr_expr, std::source_location::current())

#ifdef _WIN32
template <typename T>
struct ComReleaseDeleter {
    void operator()(T* ptr) const noexcept {
        if (ptr != nullptr) {
            ptr->Release();
        }
    }
};

template <typename T>
using UniqueComPtr = std::unique_ptr<T, ComReleaseDeleter<T>>;
#endif

[[nodiscard]] HardwareTier ClassifyHardwareTier(const SystemSpecs& specs) noexcept;
[[nodiscard]] RecommendedProfile BuildRecommendedProfile(const SystemSpecs& specs) noexcept;
[[nodiscard]] SystemSpecs ProbeHostSystemSpecs() noexcept;

} // namespace unc::core

#include "unc/core/arena_allocator.hpp"
#include "unc/core/frame_pacer.hpp"
#include "unc/core/spsc_queue.hpp"
