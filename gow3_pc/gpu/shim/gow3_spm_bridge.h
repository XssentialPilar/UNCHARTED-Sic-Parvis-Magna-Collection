// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace Gow3SpmBridge {

/// Lock-free runtime snapshot mirrored from SPMCore for scheduler, streaming, and overlay UI.
struct alignas(64) SpmRuntimeSnapshot {
    bool spm_loaded{false};
    bool cpu_heterogeneous{false};
    bool direct_storage_dll_found{false};
    bool npu_hardware_detected{false};
    bool neural_hw_accelerated{false};
    bool low_end_mode_active{false};
    uint8_t critical_load{0};
    uint8_t throttle_bg{0};
    uint8_t storage_type{0};   // 0=Unknown, 1=HDD, 2=SATA SSD, 3=NVMe SSD
    uint8_t neural_backend{0}; // SpmBackendType
    uint32_t cpu_physical_cores{0};
    uint32_t cpu_logical_cores{0};
    uint32_t active_workers{1};
    uint32_t max_streaming_slice_ms{2};
    uint32_t streaming_budget_mb{512};
    uint32_t current_vram_used_mb{0};
    uint32_t current_vram_budget_mb{0};
    uint32_t upload_quota_bytes_per_frame{3u << 20};
    float storage_bandwidth_mbps{0.0f};
    float lookahead_meters{100.0f};
    float proactive_lod_bias{0.0f};
    float predicted_vram_mb{0.0f};
    char strategy[128]{};
    char neural_backend_desc[160]{};
    char npu_name[128]{};
};

/// Loads SPMCore dynamically (optional), configures timer resolution, detects topology & storage,
/// and initializes SPM streaming + neural heuristic coordinator. Safe no-op fallback if absent.
void Initialize(const char* game_dir);

/// Shuts down SPMCore subsystems, restores timer resolution, and releases the dynamic module.
void Shutdown();

/// Recommends async pipeline compile worker count from detected physical/P-core topology.
uint32_t RecommendedCompileWorkers(uint32_t available_threads);

/// True when SPM scheduler recommends throttling background shader/warmup work for frame pacing.
bool ShouldThrottleBackgroundCompile();

/// True when Legacy / Low-end 2015 performance mode is active (explicitly or via Auto hardware check).
bool IsLowEndModeActive();

/// Per-frame streaming/eviction byte quota (1.5 MiB - 3.0 MiB) scaled by storage tier & load.
uint32_t EffectiveUploadQuotaBytesPerFrame();

/// Called once per frame at Rasterizer::NoteFrameStart (zero heap allocations).
void OnFrameBegin(float frame_time_ms);

/// Called from TextureCache budget checks (approx every 100 ms) to feed real VRAM residency
/// and OS/driver budget pressure (including integrated GPU shared-memory awareness).
void UpdateVramTelemetry(uint32_t used_mb, uint32_t budget_mb, uint32_t os_available_mb = 0,
                         bool is_integrated = false);

/// Updates completed/submitted GPU timeline fence ticks so in-flight resources are never evicted.
void UpdateGpuFenceTelemetry(uint64_t submitted_fence, uint64_t completed_fence);

/// Applies live settings changes (neural assist mode, storage profile, streaming budget).
void SyncLiveSettings();

/// Wait-free snapshot copy for UI/telemetry readers.
void GetSnapshot(SpmRuntimeSnapshot& out);

} // namespace Gow3SpmBridge
