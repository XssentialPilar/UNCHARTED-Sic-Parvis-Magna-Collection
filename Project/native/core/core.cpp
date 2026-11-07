// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Core Hardware Classifier & VTable Hook Implementation

#include "unc/core.hpp"
#include "minhook.h"

#include <algorithm>
#include <atomic>

#ifdef _WIN32
#include <intrin.h>
#endif

namespace unc::vendor::minhook {

namespace {
std::atomic<bool> g_initialized{false};
} // namespace

HookStatus Initialize() noexcept {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return HookStatus::AlreadyInitialized;
    }
    return HookStatus::Ok;
}

HookStatus Uninitialize() noexcept {
    bool expected = true;
    if (!g_initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return HookStatus::NotInitialized;
    }
    return HookStatus::Ok;
}

HookStatus CreateVTableHook(void** vtable_base, size_t slot_index, void* detour,
                            void** out_original, VTableHookRecord& out_record) noexcept {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return HookStatus::NotInitialized;
    }
    if (vtable_base == nullptr || detour == nullptr) {
        return HookStatus::InvalidArgument;
    }
    void** slot = &vtable_base[slot_index];
    out_record.vtable_slot = slot;
    out_record.detour_target = detour;
    out_record.original_target = *slot;
    out_record.enabled = false;
    if (out_original != nullptr) {
        *out_original = out_record.original_target;
    }
    return HookStatus::Ok;
}

HookStatus EnableVTableHook(VTableHookRecord& record) noexcept {
    if (record.vtable_slot == nullptr || record.detour_target == nullptr) {
        return HookStatus::NotCreated;
    }
    if (record.enabled) {
        return HookStatus::Enabled;
    }
#ifdef _WIN32
    DWORD old_protect = 0;
    if (!VirtualProtect(record.vtable_slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return HookStatus::MemoryProtectFailed;
    }
    *record.vtable_slot = record.detour_target;
    DWORD restored = 0;
    (void)VirtualProtect(record.vtable_slot, sizeof(void*), old_protect, &restored);
#else
    *record.vtable_slot = record.detour_target;
#endif
    record.enabled = true;
    return HookStatus::Ok;
}

HookStatus DisableVTableHook(VTableHookRecord& record) noexcept {
    if (record.vtable_slot == nullptr || record.original_target == nullptr) {
        return HookStatus::NotCreated;
    }
    if (!record.enabled) {
        return HookStatus::Disabled;
    }
#ifdef _WIN32
    DWORD old_protect = 0;
    if (!VirtualProtect(record.vtable_slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return HookStatus::MemoryProtectFailed;
    }
    *record.vtable_slot = record.original_target;
    DWORD restored = 0;
    (void)VirtualProtect(record.vtable_slot, sizeof(void*), old_protect, &restored);
#else
    *record.vtable_slot = record.original_target;
#endif
    record.enabled = false;
    return HookStatus::Ok;
}

} // namespace unc::vendor::minhook

namespace unc::core {

HardwareTier ClassifyHardwareTier(const SystemSpecs& specs) noexcept {
    const bool has_graphics =
        specs.supports_dx11_fl11_0 || specs.supports_vulkan_1_3 || specs.supports_dx12;
    if (!has_graphics || specs.cpu_logical_cores < 2U || specs.system_ram_mib < 3072ULL ||
        specs.dedicated_vram_mib < 768ULL) {
        return HardwareTier::Unsupported;
    }

    // Modern High-End: >= 8 GB RAM and >= 4 GB VRAM
    if (specs.system_ram_mib >= 8000ULL && specs.dedicated_vram_mib >= 3800ULL &&
        specs.cpu_logical_cores >= 6U) {
        return HardwareTier::ModernHigh_1440p_4GBPlus;
    }

    // 2015 Recommended (1080p / 60 FPS, PS4 Settings):
    // Intel Core i5-2400 / AMD FX-6300 (>= 4 logical cores), 6-8 GB RAM (>= 5600 MiB),
    // 2 GB VRAM (GTX 750 Ti / GTX 660 / Radeon HD 7850 / R7 265 / R9 270 -> >= 1792 MiB)
    if (specs.system_ram_mib >= 5600ULL && specs.dedicated_vram_mib >= 1792ULL &&
        specs.cpu_logical_cores >= 4U) {
        return HardwareTier::MidRange2015_1080p_2GB;
    }

    // 2015 Minimum (720p / 30-45 FPS, Medium-Low Quality):
    // Core 2 Quad Q8400 / Phenom II X4 945 / Core i3-2100, 4 GB RAM,
    // 1 GB VRAM (GTX 460 / GTX 550 Ti / GTX 650 / Radeon HD 5770 / HD 6790 / HD 7750)
    return HardwareTier::Entry2015_720p_1GB;
}

RecommendedProfile BuildRecommendedProfile(const SystemSpecs& specs) noexcept {
    RecommendedProfile profile{};
    profile.tier = ClassifyHardwareTier(specs);
    profile.enable_isa_trap_emulation = true;

    const uint64_t vram_mib = std::max<uint64_t>(specs.dedicated_vram_mib, 1024ULL);

    switch (profile.tier) {
        case HardwareTier::ModernHigh_1440p_4GBPlus:
            profile.preferred_api = specs.supports_vulkan_1_3
                                        ? GraphicsApi::Vulkan13
                                        : (specs.supports_dx12 ? GraphicsApi::DirectX12
                                                               : GraphicsApi::DirectX11_FL11_0);
            profile.internal_width = 2560U;
            profile.internal_height = 1440U;
            profile.target_fps = 60U;
            profile.direct_memory_pool_mib = 5056ULL;
            profile.vram_gc_trigger_mib = (vram_mib * 60ULL) / 100ULL;
            profile.vram_gc_pressure_mib = (vram_mib * 75ULL) / 100ULL;
            profile.vram_gc_critical_mib = (vram_mib * 88ULL) / 100ULL;
            profile.low_ram_mode = false;
            profile.low_vram_mode = false;
            break;

        case HardwareTier::MidRange2015_1080p_2GB:
            profile.preferred_api = specs.supports_dx11_fl11_0 ? GraphicsApi::DirectX11_FL11_0
                                                               : GraphicsApi::Vulkan13;
            profile.internal_width = 1920U;
            profile.internal_height = 1080U;
            profile.target_fps = 60U;
            profile.direct_memory_pool_mib =
                specs.system_ram_mib >= 7680ULL ? 5056ULL : 4096ULL;
            profile.vram_gc_trigger_mib = (vram_mib * 55ULL) / 100ULL;
            profile.vram_gc_pressure_mib = (vram_mib * 70ULL) / 100ULL;
            profile.vram_gc_critical_mib = (vram_mib * 82ULL) / 100ULL;
            profile.low_ram_mode = specs.system_ram_mib < 7680ULL;
            profile.low_vram_mode = vram_mib <= 2560ULL;
            break;

        case HardwareTier::Entry2015_720p_1GB:
        case HardwareTier::Unsupported:
        default:
            profile.preferred_api = GraphicsApi::DirectX11_FL11_0;
            profile.internal_width = 1280U;
            profile.internal_height = 720U;
            profile.target_fps = 30U;
            profile.direct_memory_pool_mib = 3584ULL;
            profile.vram_gc_trigger_mib = (vram_mib * 55ULL) / 100ULL;
            profile.vram_gc_pressure_mib = (vram_mib * 70ULL) / 100ULL;
            profile.vram_gc_critical_mib = (vram_mib * 82ULL) / 100ULL;
            profile.low_ram_mode = true;
            profile.low_vram_mode = true;
            break;
    }
    return profile;
}

SystemSpecs ProbeHostSystemSpecs() noexcept {
    SystemSpecs specs{};
#ifdef _WIN32
    SYSTEM_INFO sys_info{};
    GetNativeSystemInfo(&sys_info);
    specs.cpu_logical_cores =
        sys_info.dwNumberOfProcessors > 0 ? static_cast<uint32_t>(sys_info.dwNumberOfProcessors) : 4U;

    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        specs.system_ram_mib =
            static_cast<uint64_t>(mem_status.ullTotalPhys / (1024ULL * 1024ULL));
    }

    int cpu_info[4] = {0, 0, 0, 0};
    __cpuid(cpu_info, 1);
    specs.has_popcnt = (cpu_info[2] & (1 << 23)) != 0;
    specs.has_avx = (cpu_info[2] & (1 << 28)) != 0;
#endif
    return specs;
}

} // namespace unc::core
