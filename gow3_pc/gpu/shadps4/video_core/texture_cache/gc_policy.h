// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace VideoCore {

// More pressure must admit newer idle resources, rather than protecting them for longer.
// Actual Vulkan destruction is still deferred until their submitted commands complete.
constexpr std::uint64_t GcAge(bool pressured, bool aggressive) {
    return aggressive ? 16 : pressured ? 80 : 160;
}

// SuperMedo/uncharted_pc 0.3.13 (GPL-2.0-or-later): exclude images already queued for
// destruction so several submissions do not evict more textures for the same pending bytes.
constexpr std::uint64_t EffectiveMemoryUsage(std::uint64_t usage, std::uint64_t pending_free) {
    return usage > pending_free ? usage - pending_free : 0;
}

struct GcWatermarks {
    std::uint64_t trigger_gc_memory;
    std::uint64_t pressure_gc_memory;
    std::uint64_t critical_gc_memory;
};

// Uncharted: Sic Parvis Magna Collection — 2015+ 1 GB / 2 GB VRAM-aware GC watermarks.
// On 1 GB (GTX 460/550 Ti/650, HD 5770/6790/7750) and 2 GB (GTX 750 Ti/660, HD 7850/R7 265/R9 270)
// GPUs, fixed 1.5 GiB / 3 GiB floors would exceed total physical VRAM and prevent GC from running.
constexpr GcWatermarks ComputeGcWatermarks(std::uint64_t device_local_memory,
                                           bool low_vram_override = false) noexcept {
    constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
    constexpr std::uint64_t kGiB = 1024ULL * kMiB;
    if (device_local_memory == 0) {
        return {512 * kMiB, 768 * kMiB, 896 * kMiB};
    }
    if (low_vram_override || device_local_memory <= 2560 * kMiB) {
        return {
            (device_local_memory * 55) / 100,
            (device_local_memory * 70) / 100,
            (device_local_memory * 82) / 100,
        };
    }
    if (device_local_memory <= 6 * kGiB) {
        return {
            (device_local_memory * 60) / 100,
            (device_local_memory * 75) / 100,
            (device_local_memory * 88) / 100,
        };
    }
    const std::uint64_t min_spacing_expected = device_local_memory - kGiB;
    const std::uint64_t min_spacing_critical = device_local_memory - 512 * kMiB;
    const std::uint64_t mem_threshold =
        device_local_memory < 8 * kGiB ? device_local_memory : 8 * kGiB;
    const std::uint64_t min_vacancy_expected = (6 * mem_threshold) / 10;
    const std::uint64_t min_vacancy_critical = (2 * mem_threshold) / 10;
    const std::uint64_t expected_candidate =
        (device_local_memory - min_vacancy_expected) < min_spacing_expected
            ? (device_local_memory - min_vacancy_expected)
            : min_spacing_expected;
    const std::uint64_t critical_candidate =
        (device_local_memory - min_vacancy_critical) < min_spacing_critical
            ? (device_local_memory - min_vacancy_critical)
            : min_spacing_critical;
    const std::uint64_t default_pressure = kGiB + 512 * kMiB;
    const std::uint64_t default_critical = 3 * kGiB;
    return {
        (device_local_memory - mem_threshold) / 2 > (device_local_memory * 50) / 100
            ? (device_local_memory - mem_threshold) / 2
            : (device_local_memory * 50) / 100,
        expected_candidate > default_pressure ? expected_candidate : default_pressure,
        critical_candidate > default_critical ? critical_candidate : default_critical,
    };
}

} // namespace VideoCore
