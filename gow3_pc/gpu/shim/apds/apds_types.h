// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Types & Data Models
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace APDS {

enum class TelemetryMode : uint32_t {
    Disabled = 0,
    Light    = 1,
    Standard = 2,
    Deep     = 3,
    Forensic = 4
};

enum class EventSeverity : uint32_t {
    Trace       = 0,
    Debug       = 1,
    Info        = 2,
    Warning     = 3,
    Error       = 4,
    Critical    = 5
};

enum class EventCategory : uint32_t {
    Trace       = 0,
    Debug       = 1,
    Info        = 2,
    Warning     = 3,
    Error       = 4,
    Critical    = 5,
    Performance = 6,
    Gpu         = 7,
    Cpu         = 8,
    Memory      = 9,
    Io          = 10,
    Render      = 11,
    Shader      = 12,
    Resource    = 13,
    Input       = 14,
    Thread      = 15,
    Sync        = 16,
    Crash       = 17,
    Hang        = 18,
    Recovery    = 19
};

inline const char* ToString(EventSeverity severity) noexcept {
    switch (severity) {
        case EventSeverity::Trace:    return "TRACE";
        case EventSeverity::Debug:    return "DEBUG";
        case EventSeverity::Info:     return "INFO";
        case EventSeverity::Warning:  return "WARNING";
        case EventSeverity::Error:    return "ERROR";
        case EventSeverity::Critical: return "CRITICAL";
        default:                      return "UNKNOWN";
    }
}

inline const char* ToString(EventCategory cat) noexcept {
    switch (cat) {
        case EventCategory::Trace:       return "TRACE";
        case EventCategory::Debug:       return "DEBUG";
        case EventCategory::Info:        return "INFO";
        case EventCategory::Warning:     return "WARNING";
        case EventCategory::Error:       return "ERROR";
        case EventCategory::Critical:    return "CRITICAL";
        case EventCategory::Performance: return "PERFORMANCE";
        case EventCategory::Gpu:         return "GPU";
        case EventCategory::Cpu:         return "CPU";
        case EventCategory::Memory:      return "MEMORY";
        case EventCategory::Io:          return "IO";
        case EventCategory::Render:      return "RENDER";
        case EventCategory::Shader:      return "SHADER";
        case EventCategory::Resource:    return "RESOURCE";
        case EventCategory::Input:       return "INPUT";
        case EventCategory::Thread:      return "THREAD";
        case EventCategory::Sync:        return "SYNC";
        case EventCategory::Crash:       return "CRASH";
        case EventCategory::Hang:        return "HANG";
        case EventCategory::Recovery:    return "RECOVERY";
        default:                         return "GENERAL";
    }
}

inline const char* ToString(TelemetryMode mode) noexcept {
    switch (mode) {
        case TelemetryMode::Disabled: return "DISABLED";
        case TelemetryMode::Light:    return "LIGHT";
        case TelemetryMode::Standard: return "STANDARD";
        case TelemetryMode::Deep:     return "DEEP";
        case TelemetryMode::Forensic: return "FORENSIC";
        default:                      return "UNKNOWN";
    }
}

struct CpuTopology {
    std::string vendor;
    std::string brand;
    uint32_t packages{1};
    uint32_t physical_cores{0};
    uint32_t logical_processors{0};
    uint32_t numa_nodes{1};
    uint64_t l1_cache_bytes{0};
    uint64_t l2_cache_bytes{0};
    uint64_t l3_cache_bytes{0};
    bool has_avx{false};
    bool has_avx2{false};
    bool has_avx512{false};
    bool has_fma{false};
};

struct GpuInfo {
    std::string adapter_name;
    std::string driver_version;
    uint32_t vendor_id{0};
    uint32_t device_id{0};
    uint64_t dedicated_vram_bytes{0};
    uint64_t shared_vram_bytes{0};
    bool is_discrete{true};
    bool is_nvml_supported{false};
};

struct HardwareProfile {
    CpuTopology cpu;
    GpuInfo primary_gpu;
    std::string os_version;
    uint64_t total_ram_bytes{0};
    uint32_t display_width{1920};
    uint32_t display_height{1080};
    uint32_t display_refresh_hz{60};
};

struct CpuSample {
    int64_t timestamp_us{0};
    float total_cpu_percent{0.0f};
    float process_cpu_percent{0.0f};
    float cpu_temp_c{-1.0f};
    std::vector<float> per_core_percent;
};

struct GpuSample {
    int64_t timestamp_us{0};
    float gpu_engine_3d_percent{-1.0f};
    float gpu_temp_c{-1.0f};
    float core_clock_mhz{-1.0f};
    float mem_clock_mhz{-1.0f};
    float power_watts{-1.0f};
    float power_limit_watts{-1.0f};
    uint64_t vram_local_used_bytes{0};
    uint64_t vram_local_budget_bytes{0};
    uint64_t vram_non_local_used_bytes{0};
};

struct MemorySample {
    int64_t timestamp_us{0};
    uint64_t process_working_set_bytes{0};
    uint64_t process_peak_working_set_bytes{0};
    uint64_t process_private_commit_bytes{0};
    uint64_t system_available_ram_bytes{0};
    uint32_t system_memory_load_percent{0};
};

struct FrameSample {
    uint64_t frame_index{0};
    int64_t start_time_us{0};
    int64_t present_time_us{0};
    double frame_time_ms{0.0};
    double cpu_time_ms{0.0};
    double gpu_time_ms{0.0};
    double jitter_ms{0.0};
    uint32_t draw_calls{0};
    uint32_t compute_dispatches{0};
    bool is_stutter{false};
};

struct ResourceEvent {
    int64_t timestamp_us{0};
    uint64_t frame_index{0};
    std::string action; // "ALLOC", "UPLOAD", "EVICT", "FREE"
    std::string resource_type; // "TEXTURE", "BUFFER", "RT", "SHADER"
    std::string identifier;
    uint64_t size_bytes{0};
    uint32_t mips{0};
    std::string details;
};

struct IoEvent {
    int64_t timestamp_us{0};
    uint64_t frame_index{0};
    std::string operation; // "READ", "WRITE", "OPEN"
    std::string path;
    uint64_t bytes{0};
    double duration_us{0.0};
    bool success{true};
};

struct StructuredEvent {
    int64_t timestamp_us{0};
    std::string timestamp_iso;
    uint64_t frame_index{0};
    uint32_t thread_id{0};
    EventCategory category{EventCategory::Info};
    EventSeverity severity{EventSeverity::Info};
    std::string code;
    std::string message;
    double duration_us{0.0};
    std::string context_json;
};

struct StutterIncident {
    uint64_t frame_index{0};
    int64_t timestamp_us{0};
    double frame_time_ms{0.0};
    double baseline_ms{0.0};
    float concurrent_cpu_percent{0.0f};
    float concurrent_gpu_percent{0.0f};
    uint64_t concurrent_io_bytes{0};
    uint64_t concurrent_upload_bytes{0};
    std::string primary_suspicion;
    std::string details;
};

} // namespace APDS
