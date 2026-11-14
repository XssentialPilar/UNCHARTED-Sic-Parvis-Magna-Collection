// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: numbers for the performance overlay (gow3_overlay.cpp). A thread started once samples
// CPU, RAM, GPU use and their temperatures (Windows counters, NVIDIA's driver) and VRAM
// (VK_EXT_memory_budget) once a second while the overlay shows one of them (20 Oct 2026;
// GOW3_PERF_SAMPLER_PAUSE=0: twice a second, always); a value that cannot be measured stays
// negative and shows as "n/a".
//
// 24 Oct 2026: GPU and CPU temperatures next to their use (nvml.dll for the graphics card, the
// ACPI thermal zone for the processor), and the graphics card shows as "GPU" instead of its
// full name so the counter fits one line.
//
// 24 Oct 2026: RAM is the game's own memory (its working set), shown without the system's total
// ("RAM 6.1 GB") so the line stays short.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Vulkan {
class Instance;
}

namespace Gow3PerfStats {

/// Overlay items (Gow3Settings::Values::overlay_items).
enum Item : unsigned {
    Fps = 1u << 0,
    FrameTime = 1u << 1,
    Upscaler = 1u << 2,
    Gpu = 1u << 3,
    Cpu = 1u << 4,
    Ram = 1u << 5,
    Vram = 1u << 6,
    /// Frame-time percentiles over the last seconds (P50/P95/P99), not just the average.
    Percentiles = 1u << 7,
    /// The 1% and 0.1% lows (the slowest frames), where hitches actually show.
    Lows = 1u << 8,
    AllItems = (1u << 9) - 1,
};
inline constexpr unsigned DefaultItems = Fps | FrameTime | Upscaler;

struct Sample {
    float fps = 0.0f, frame_ms = 0.0f;
    std::string api_tag;
    std::string upscaler;
    std::string gpu_name;
    float gpu_percent = -1.0f, cpu_percent = -1.0f;
    float gpu_temp_c = -1.0f, cpu_temp_c = -1.0f;
    // Frame-time distribution over the sampling window; <= 0 when not measured yet.
    float p50_ms = -1.0f, p95_ms = -1.0f, p99_ms = -1.0f;
    float low1_ms = -1.0f, low01_ms = -1.0f, jitter_ms = -1.0f;
    // RAM is the whole system (not this process); VRAM is the card's memory budget.
    double ram_used_gb = -1.0, ram_total_gb = -1.0, vram_used_gb = -1.0, vram_total_gb = -1.0;
    double vram_driver_used_gb = -1.0, vram_driver_total_gb = -1.0;
    bool is_vram_capped = false;
    // Streamline Reflex + Frame Generation & GPU clock/power/thermal telemetry.
    float pc_latency_ms = -1.0f;
    bool fg_active_now = false;
    int active_fg_multiplier = 1;
    uint32_t gpu_clock_mhz = 0;
    uint32_t mem_clock_mhz = 0;
    float gpu_power_watts = -1.0f;
    bool gpu_thermal_throttled = false;
};

/// Process CPU time over wall time on every core, 0 to 100; -1 without a wall interval.
inline float CpuPercent(uint64_t process_delta, uint64_t wall_delta, unsigned cores) {
    if (!wall_delta || !cores) {
        return -1.0f;
    }
    return std::clamp(float(double(process_delta) * 100.0 / (double(wall_delta) * cores)), 0.0f, 100.0f);
}

/// The overlay text for the items in `mask`: one line each, or one line when `row`.
inline std::string Format(const Sample& s, unsigned mask, bool row) {
    std::string out;
    char part[160];
    const auto add = [&] {
        if (!out.empty()) {
            out += row ? "   " : "\n";
        }
        out += part;
    };
    const auto pair = [](char* to, size_t size, const char* label, double used, double total) {
        if (used < 0.0) {
            std::snprintf(to, size, "%s n/a", label);
        } else if (total < 0.0) {
            std::snprintf(to, size, "%s %.1f GB", label, used);
        } else {
            std::snprintf(to, size, "%s %.1f / %.1f GB", label, used, total);
        }
    };
    // "°C" as UTF-8; the overlay's DejaVu Sans font has it.
    constexpr const char* degree = "\xc2\xb0" "C";
    if (!s.api_tag.empty() && (mask & (Fps | FrameTime | Upscaler))) {
        std::snprintf(part, sizeof(part), "%s", s.api_tag.c_str());
        add();
    }
    if (mask & Fps) {
        if (s.fg_active_now && s.active_fg_multiplier > 1) {
            std::snprintf(part, sizeof(part), "%.0f FPS (FG %dx)", s.fps, s.active_fg_multiplier);
        } else {
            std::snprintf(part, sizeof(part), "%.0f FPS", s.fps);
        }
        add();
    }
    if (mask & FrameTime) {
        if (s.pc_latency_ms > 0.0f) {
            std::snprintf(part, sizeof(part), "%.1f ms (PCL %.1f ms)", s.frame_ms, s.pc_latency_ms);
        } else {
            std::snprintf(part, sizeof(part), "%.1f ms", s.frame_ms);
        }
        add();
    }
    if (mask & Upscaler) {
        std::snprintf(part, sizeof(part), "%s", s.upscaler.empty() ? "Native" : s.upscaler.c_str());
        add();
    }
    // The card's name is not shown (24 Oct 2026): "GPU" keeps the one-line counter short.
    if (mask & Gpu) {
        if (s.gpu_percent < 0.0f) {
            std::snprintf(part, sizeof(part), "GPU n/a");
        } else if (s.gpu_temp_c < 0.0f) {
            std::snprintf(part, sizeof(part), "GPU %.0f%%", s.gpu_percent);
        } else if (s.gpu_clock_mhz > 0 && s.gpu_power_watts > 0.0f) {
            std::snprintf(part, sizeof(part), "GPU %.0f%% %.0f%s (%u MHz %.0fW%s)",
                          s.gpu_percent, s.gpu_temp_c, degree, s.gpu_clock_mhz,
                          s.gpu_power_watts, s.gpu_thermal_throttled ? " THRM" : "");
        } else {
            std::snprintf(part, sizeof(part), "GPU %.0f%% %.0f%s", s.gpu_percent, s.gpu_temp_c, degree);
        }
        add();
    }
    if (mask & Cpu) {
        if (s.cpu_percent < 0.0f) {
            std::snprintf(part, sizeof(part), "CPU n/a");
        } else if (s.cpu_temp_c < 0.0f) {
            std::snprintf(part, sizeof(part), "CPU %.0f%%", s.cpu_percent);
        } else {
            std::snprintf(part, sizeof(part), "CPU %.0f%% %.0f%s", s.cpu_percent, s.cpu_temp_c, degree);
        }
        add();
    }
    if (mask & Ram) {
        // The system's RAM in use, without the total (24 Oct 2026).
        pair(part, sizeof(part), "RAM", s.ram_used_gb, -1.0);
        add();
    }
    if (mask & Vram) {
        if (s.is_vram_capped && s.vram_total_gb > 0.0 && s.vram_total_gb <= 2.5) {
            const double engine_mb = s.vram_used_gb * 1024.0;
            const double cap_mb = s.vram_total_gb * 1024.0;
            if (s.vram_driver_used_gb > 0.0) {
                std::snprintf(part, sizeof(part), "VRAM %.0f/%.0f MB (Motor) [Drv: %.1f GB]",
                              engine_mb, cap_mb, s.vram_driver_used_gb);
            } else {
                std::snprintf(part, sizeof(part), "VRAM %.0f/%.0f MB (Motor)", engine_mb, cap_mb);
            }
        } else {
            pair(part, sizeof(part), "VRAM", s.vram_used_gb, s.vram_total_gb);
        }
        add();
    }
    // The distribution, not only the average: a 57 FPS average made of steady 17.5 ms frames is
    // not the same as one made of 16.7 ms frames with 60 ms hitches, and only the percentiles and
    // the lows tell them apart (what the acceptance matrix asks to measure).
    if (mask & Percentiles) {
        if (s.p50_ms < 0.0f) {
            std::snprintf(part, sizeof(part), "P50/P95/P99 n/a");
        } else {
            std::snprintf(part, sizeof(part), "P50 %.1f  P95 %.1f  P99 %.1f ms", s.p50_ms, s.p95_ms,
                          s.p99_ms);
        }
        add();
    }
    if (mask & Lows) {
        if (s.low1_ms < 0.0f) {
            std::snprintf(part, sizeof(part), "1%%/0.1%% low n/a");
        } else {
            std::snprintf(part, sizeof(part), "1%% low %.0f FPS  0.1%% low %.0f FPS",
                          s.low1_ms > 0.0f ? 1000.0f / s.low1_ms : 0.0f,
                          s.low01_ms > 0.0f ? 1000.0f / s.low01_ms : 0.0f);
        }
        add();
    }
    return out;
}

/// Starts the sampling thread the first time. Every call also keeps it sampling: it sleeps
/// once no call came for 2 s (the overlay calls it on each frame that shows a sampled item).
void Start(const Vulkan::Instance& instance);
/// The latest CPU, RAM, GPU and VRAM values, with their temperatures (the caller fills FPS,
/// frametime and upscaler).
Sample Latest();

} // namespace Gow3PerfStats
