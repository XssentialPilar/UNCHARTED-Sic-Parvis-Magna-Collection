// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_spm_bridge.h"

#include "gow3_perf_stats.h"
#include "gow3_settings.h"
#include "spm/spm_core.h"
#include "spm/spm_streaming.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Gow3SpmBridge {
namespace {

using FnInitTimer = void (*)(void);
using FnShutdownTimer = void (*)(void);
using FnDetectCpuTopology = int (*)(SpmCpuTopology*);
using FnEvaluateScheduler = void (*)(const SpmCpuTopology*, const SpmFrameTelemetry*, SpmSchedulerDecision*);
using FnDetectHardwareCaps = int (*)(const char*, SpmHardwareCapabilities*);
using FnGetStorageTypeName = const char* (*)(SpmStorageType);
using FnStreamingInit = int (*)(uint64_t, uint32_t, uint32_t);
using FnStreamingShutdown = void (*)(void);
using FnStreamingProcessSlice = uint32_t (*)(uint32_t);
using FnStreamingEnforceBudget = int (*)(void);
using FnStreamingGetStats = void (*)(SpmStreamingStats*);
using FnStreamingUpdateGpuBudget = void (*)(uint64_t, uint64_t, int);
using FnStreamingSetGpuFence = void (*)(uint64_t, uint64_t);
using FnStreamingSetHysteresis = void (*)(uint32_t);
using FnNeuralInit = int (*)(SpmNeuralAssistMode);
using FnNeuralShutdown = void (*)(void);
using FnNeuralSetMode = void (*)(SpmNeuralAssistMode);
using FnNeuralPredict = int (*)(const SpmPredictorInput*, SpmPredictorOutput*);
using FnNeuralGetBackendInfo = int (*)(SpmNeuralBackendInfo*);

struct SpmApiTable {
#ifdef _WIN32
    HMODULE module{nullptr};
#else
    void* module{nullptr};
#endif
    FnInitTimer init_timer{nullptr};
    FnShutdownTimer shutdown_timer{nullptr};
    FnDetectCpuTopology detect_cpu_topology{nullptr};
    FnEvaluateScheduler evaluate_scheduler{nullptr};
    FnDetectHardwareCaps detect_hardware_caps{nullptr};
    FnGetStorageTypeName get_storage_type_name{nullptr};
    FnStreamingInit streaming_init{nullptr};
    FnStreamingShutdown streaming_shutdown{nullptr};
    FnStreamingProcessSlice streaming_process_slice{nullptr};
    FnStreamingEnforceBudget streaming_enforce_budget{nullptr};
    FnStreamingGetStats streaming_get_stats{nullptr};
    FnStreamingUpdateGpuBudget streaming_update_gpu_budget{nullptr};
    FnStreamingSetGpuFence streaming_set_gpu_fence{nullptr};
    FnStreamingSetHysteresis streaming_set_hysteresis{nullptr};
    FnNeuralInit neural_init{nullptr};
    FnNeuralShutdown neural_shutdown{nullptr};
    FnNeuralSetMode neural_set_mode{nullptr};
    FnNeuralPredict neural_predict{nullptr};
    FnNeuralGetBackendInfo neural_get_backend_info{nullptr};
};

std::mutex g_lifecycle_mutex;
SpmApiTable g_api{};
SpmCpuTopology g_topology{};
SpmHardwareCapabilities g_caps{};

std::atomic<bool> g_initialized{false};
std::atomic<bool> g_spm_loaded{false};
std::atomic<bool> g_throttle_bg{false};
std::atomic<uint32_t> g_upload_quota_bytes{3u << 20};
std::atomic<uint32_t> g_vram_used_mb{0};
std::atomic<uint32_t> g_vram_budget_mb{0};
std::atomic<uint64_t> g_frame_counter{0};

std::atomic<int> g_applied_neural_mode{-1};
std::atomic<int> g_applied_storage_mode{-1};
std::atomic<int> g_applied_low_end_mode{-1};
std::atomic<bool> g_low_end_active{false};
std::atomic<uint32_t> g_applied_stream_budget_mb{0};

std::atomic<float> g_cached_p95_ms{16.6667f};
std::atomic<float> g_cached_cpu_pct{0.0f};
std::atomic<bool> g_cached_thermal_throttle{false};

// Seqlock-protected snapshot so readers never allocate or block.
std::atomic<uint64_t> g_snapshot_seq{0};
SpmRuntimeSnapshot g_snapshot{};

void CopyStr(char* dst, size_t dst_size, const char* src) noexcept {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dst_size, "%s", src);
}

template <typename Fn>
Fn ResolveSymbol(void* mod, const char* name) noexcept {
    if (!mod || !name) {
        return nullptr;
    }
#ifdef _WIN32
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(mod), name);
    Fn fn{};
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
#else
    void* sym = dlsym(mod, name);
    Fn fn{};
    std::memcpy(&fn, &sym, sizeof(fn));
    return fn;
#endif
}

bool LoadSpmLibrary() noexcept {
#ifdef _WIN32
    wchar_t exe_path[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    HMODULE mod = nullptr;
    if (len > 0 && len < MAX_PATH) {
        wchar_t* slash = std::wcsrchr(exe_path, L'\\');
        if (slash) {
            *slash = L'\0';
        }
        static constexpr std::array<const wchar_t*, 6> kRelCandidates = {
            L"\\SPMCore.dll",
            L"\\libSPMCore.dll",
            L"\\..\\SPMCore.dll",
            L"\\..\\spm_core\\build-release\\SPMCore.dll",
            L"\\..\\..\\Project\\spm_core\\build-release\\SPMCore.dll",
            L"\\..\\..\\..\\Project\\spm_core\\build-release\\SPMCore.dll",
        };
        for (const wchar_t* suffix : kRelCandidates) {
            wchar_t candidate[MAX_PATH]{};
            if (std::swprintf(candidate, MAX_PATH, L"%ls%ls", exe_path, suffix) > 0) {
                mod = LoadLibraryW(candidate);
                if (mod) {
                    break;
                }
            }
        }
    }
    if (!mod) {
        mod = LoadLibraryA("SPMCore.dll");
    }
    if (!mod) {
        mod = LoadLibraryA("libSPMCore.dll");
    }
    if (!mod) {
        return false;
    }
    g_api.module = mod;
#else
    static constexpr std::array<const char*, 5> kUnixCandidates = {
        "libSPMCore.so",
        "./libSPMCore.so",
        "./core/libSPMCore.so",
        "../lib/libSPMCore.so",
        "../../Project/spm_core/build-linux/libSPMCore.so",
    };
    void* mod = nullptr;
    for (const char* path : kUnixCandidates) {
        mod = dlopen(path, RTLD_LAZY);
        if (mod) {
            break;
        }
    }
    if (!mod) {
        return false;
    }
    g_api.module = mod;
#endif

    void* const handle = static_cast<void*>(g_api.module);
    g_api.init_timer = ResolveSymbol<FnInitTimer>(handle, "SPM_InitTimerResolution");
    g_api.shutdown_timer = ResolveSymbol<FnShutdownTimer>(handle, "SPM_ShutdownTimerResolution");
    g_api.detect_cpu_topology = ResolveSymbol<FnDetectCpuTopology>(handle, "SPM_DetectCpuTopology");
    g_api.evaluate_scheduler = ResolveSymbol<FnEvaluateScheduler>(handle, "SPM_EvaluateScheduler");
    g_api.detect_hardware_caps = ResolveSymbol<FnDetectHardwareCaps>(handle, "SPM_DetectHardwareCapabilities");
    g_api.get_storage_type_name = ResolveSymbol<FnGetStorageTypeName>(handle, "SPM_GetStorageTypeName");
    g_api.streaming_init = ResolveSymbol<FnStreamingInit>(handle, "SPM_StreamingInit");
    g_api.streaming_shutdown = ResolveSymbol<FnStreamingShutdown>(handle, "SPM_StreamingShutdown");
    g_api.streaming_process_slice =
        ResolveSymbol<FnStreamingProcessSlice>(handle, "SPM_StreamingProcessTimeSlice");
    g_api.streaming_enforce_budget =
        ResolveSymbol<FnStreamingEnforceBudget>(handle, "SPM_StreamingEnforceBudget");
    g_api.streaming_get_stats = ResolveSymbol<FnStreamingGetStats>(handle, "SPM_StreamingGetStats");
    g_api.streaming_update_gpu_budget =
        ResolveSymbol<FnStreamingUpdateGpuBudget>(handle, "SPM_StreamingUpdateGpuBudget");
    g_api.streaming_set_gpu_fence =
        ResolveSymbol<FnStreamingSetGpuFence>(handle, "SPM_StreamingSetGpuFence");
    g_api.streaming_set_hysteresis =
        ResolveSymbol<FnStreamingSetHysteresis>(handle, "SPM_StreamingSetHysteresisFrames");
    g_api.neural_init = ResolveSymbol<FnNeuralInit>(handle, "SPM_NeuralAssistInit");
    g_api.neural_shutdown = ResolveSymbol<FnNeuralShutdown>(handle, "SPM_NeuralAssistShutdown");
    g_api.neural_set_mode = ResolveSymbol<FnNeuralSetMode>(handle, "SPM_NeuralAssistSetMode");
    g_api.neural_predict = ResolveSymbol<FnNeuralPredict>(handle, "SPM_NeuralAssistPredict");
    g_api.neural_get_backend_info = ResolveSymbol<FnNeuralGetBackendInfo>(handle, "SPM_NeuralAssistGetBackendInfo");

    return g_api.detect_cpu_topology != nullptr && g_api.evaluate_scheduler != nullptr;
}

SpmStorageType EffectiveStorageTier() noexcept {
    const int setting = Gow3Settings::Get().storage_optimization.load(std::memory_order_relaxed);
    if (setting == 1) {
        return SPM_STORAGE_NVME_SSD;
    }
    if (setting == 2) {
        return SPM_STORAGE_SATA_SSD;
    }
    if (setting == 3) {
        return SPM_STORAGE_HDD;
    }
    return g_caps.storageType;
}

uint32_t ConfiguredStreamingBudgetMb() noexcept {
    static const uint32_t env_budget_mb = []() -> uint32_t {
        if (const char* env = std::getenv("GOW3_STREAMING_BUDGET_MB"); env && env[0]) {
            const unsigned long mb = std::strtoul(env, nullptr, 10);
            if (mb >= 64 && mb <= 16384) {
                return static_cast<uint32_t>(mb);
            }
        }
        return 0u;
    }();
    if (env_budget_mb != 0u) {
        return env_budget_mb;
    }
    const int sb = Gow3Settings::Get().streaming_budget_mb.load(std::memory_order_relaxed);
    if (sb >= 64 && sb <= 16384) {
        return static_cast<uint32_t>(sb);
    }
    const int vb = Gow3Settings::Get().vram_budget_mb.load(std::memory_order_relaxed);
    if (vb >= 256 && vb <= 16384) {
        return static_cast<uint32_t>(vb);
    }
    return 512u;
}

bool ComputeLowEndActive() noexcept {
    const int mode = Gow3Settings::Get().low_end_mode.load(std::memory_order_relaxed);
    if (mode == 1) {
        return true;
    }
    if (mode == 2) {
        return false;
    }
    const uint32_t vram_mb = g_vram_budget_mb.load(std::memory_order_relaxed);
    return (g_topology.physicalCores > 0 && g_topology.physicalCores <= 4u) ||
           (g_caps.totalPhysicalRamMb > 0 && g_caps.totalPhysicalRamMb <= 8192u) ||
           (vram_mb > 0 && vram_mb <= 2048u);
}

uint32_t UploadQuotaForState(SpmStorageType storage, bool throttle) noexcept {
    uint32_t base_kb = 2560u; // 2.5 MiB default target
    switch (storage) {
    case SPM_STORAGE_NVME_SSD:
        base_kb = 3072u; // 3.0 MiB max
        break;
    case SPM_STORAGE_SATA_SSD:
        base_kb = 2560u; // 2.5 MiB
        break;
    case SPM_STORAGE_HDD:
        base_kb = 1536u; // 1.5 MiB sequential to avoid HDD seek hitching
        break;
    default:
        base_kb = 2560u;
        break;
    }
    if (throttle || g_low_end_active.load(std::memory_order_relaxed)) {
        base_kb = std::min(base_kb, 1536u);
    }
    return base_kb << 10;
}

uint32_t WorkersForStorage(const SpmCpuTopology& topo, SpmStorageType storage) noexcept {
    if (storage == SPM_STORAGE_HDD) {
        return 1u;
    }
    const uint32_t phys = topo.physicalCores > 0 ? topo.physicalCores : 2u;
    const uint32_t cap = (storage == SPM_STORAGE_NVME_SSD) ? 4u : 3u;
    return std::clamp(phys / 2u, 1u, cap);
}

SpmNeuralAssistMode MapNeuralMode(int setting) noexcept {
    if (setting == 1) {
        return SPM_NEURAL_MODE_ENABLED;
    }
    if (setting == 2) {
        return SPM_NEURAL_MODE_DISABLED;
    }
    return SPM_NEURAL_MODE_AUTO;
}

std::mutex g_publish_mutex;
void PublishSnapshot(const SpmRuntimeSnapshot& next) noexcept {
    std::scoped_lock lock{g_publish_mutex};
    const uint64_t seq = g_snapshot_seq.load(std::memory_order_relaxed);
    g_snapshot_seq.store(seq + 1, std::memory_order_release);
    g_snapshot = next;
    g_snapshot_seq.store(seq + 2, std::memory_order_release);
}

void FillBaseSnapshot(SpmRuntimeSnapshot& s) noexcept {
    s.spm_loaded = g_spm_loaded.load(std::memory_order_relaxed);
    s.cpu_heterogeneous = g_topology.isHeterogeneous != 0;
    s.direct_storage_dll_found = g_caps.storageDirectStorageSupported != 0;
    s.npu_hardware_detected = g_caps.npuSupported != 0;
    s.low_end_mode_active = g_low_end_active.load(std::memory_order_relaxed);
    s.storage_type = static_cast<uint8_t>(EffectiveStorageTier());
    s.cpu_physical_cores = g_topology.physicalCores;
    s.cpu_logical_cores = g_topology.logicalCores;
    s.storage_bandwidth_mbps = g_caps.storageEstimatedBandwidthMBps;
    s.streaming_budget_mb = g_applied_stream_budget_mb.load(std::memory_order_relaxed);
    s.current_vram_used_mb = g_vram_used_mb.load(std::memory_order_relaxed);
    s.current_vram_budget_mb = g_vram_budget_mb.load(std::memory_order_relaxed);
    s.upload_quota_bytes_per_frame = g_upload_quota_bytes.load(std::memory_order_relaxed);
    CopyStr(s.npu_name, sizeof(s.npu_name), g_caps.npuName);
}

void RefreshBackendInfoLocked(SpmRuntimeSnapshot& s) noexcept {
    if (g_api.neural_get_backend_info) {
        SpmNeuralBackendInfo info{};
        if (g_api.neural_get_backend_info(&info) != 0) {
            s.neural_hw_accelerated = info.hardwareAccelerated != 0;
            s.neural_backend = static_cast<uint8_t>(info.backend);
            CopyStr(s.neural_backend_desc, sizeof(s.neural_backend_desc), info.description);
            return;
        }
    }
    s.neural_hw_accelerated = false;
    s.neural_backend = SPM_BACKEND_CPU_CONVENTIONAL;
    CopyStr(s.neural_backend_desc, sizeof(s.neural_backend_desc),
            "CPU heuristic fallback (SPMCore absent or neutral)");
}

void ApplyLiveSettingsIfNeeded(bool force_streaming_reinit) noexcept {
    if (!g_spm_loaded.load(std::memory_order_relaxed)) {
        return;
    }
    const auto& settings = Gow3Settings::Get();
    const int neural_mode = settings.neural_assist_mode.load(std::memory_order_relaxed);
    const int storage_mode = settings.storage_optimization.load(std::memory_order_relaxed);
    const int low_end_mode = settings.low_end_mode.load(std::memory_order_relaxed);
    const uint32_t budget_mb = ConfiguredStreamingBudgetMb();

    const bool neural_changed = neural_mode != g_applied_neural_mode.load(std::memory_order_relaxed);
    const bool storage_changed = storage_mode != g_applied_storage_mode.load(std::memory_order_relaxed);
    const bool low_end_changed = low_end_mode != g_applied_low_end_mode.load(std::memory_order_relaxed);
    const bool budget_changed = budget_mb != g_applied_stream_budget_mb.load(std::memory_order_relaxed);

    if (!force_streaming_reinit && !neural_changed && !storage_changed && !low_end_changed && !budget_changed) {
        return;
    }

    std::scoped_lock lock{g_lifecycle_mutex};
    if (!g_spm_loaded.load(std::memory_order_relaxed)) {
        return;
    }

    if (low_end_changed || force_streaming_reinit) {
        const bool active = ComputeLowEndActive();
        g_low_end_active.store(active, std::memory_order_relaxed);
        g_applied_low_end_mode.store(low_end_mode, std::memory_order_relaxed);
        if (g_api.streaming_set_hysteresis) {
            g_api.streaming_set_hysteresis(active ? 16u : 8u);
        }
        g_upload_quota_bytes.store(
            UploadQuotaForState(EffectiveStorageTier(), g_throttle_bg.load(std::memory_order_relaxed)),
            std::memory_order_relaxed);
    }

    if (neural_changed && g_api.neural_set_mode) {
        g_api.neural_set_mode(MapNeuralMode(neural_mode));
        g_applied_neural_mode.store(neural_mode, std::memory_order_relaxed);
    }

    if (force_streaming_reinit || storage_changed || budget_changed) {
        const SpmStorageType tier = EffectiveStorageTier();
        const uint32_t workers = WorkersForStorage(g_topology, tier);
        if (!force_streaming_reinit && g_api.streaming_shutdown) {
            g_api.streaming_shutdown();
        }
        if (g_api.streaming_init) {
            g_api.streaming_init(budget_mb, 3u, workers);
        }
        g_applied_storage_mode.store(storage_mode, std::memory_order_relaxed);
        g_applied_stream_budget_mb.store(budget_mb, std::memory_order_relaxed);
        g_upload_quota_bytes.store(
            UploadQuotaForState(tier, g_throttle_bg.load(std::memory_order_relaxed)),
            std::memory_order_relaxed);
    }

    SpmRuntimeSnapshot snap = g_snapshot;
    FillBaseSnapshot(snap);
    RefreshBackendInfoLocked(snap);
    PublishSnapshot(snap);
}

} // namespace

void Initialize(const char* game_dir) {
    std::scoped_lock lock{g_lifecycle_mutex};
    if (g_initialized.load(std::memory_order_relaxed)) {
        return;
    }

    g_topology = {};
    g_caps = {};
    const bool loaded = LoadSpmLibrary();
    g_spm_loaded.store(loaded, std::memory_order_release);

    if (loaded) {
        if (g_api.init_timer) {
            g_api.init_timer();
        }
        if (!g_api.detect_cpu_topology || g_api.detect_cpu_topology(&g_topology) == 0) {
            const uint32_t hw = std::max(1u, std::thread::hardware_concurrency());
            g_topology.logicalCores = hw;
            g_topology.physicalCores = std::max(1u, hw / 2u);
            g_topology.maxWorkerThreads = std::max(1u, hw - 1u);
        }
        if (g_api.detect_hardware_caps) {
            g_api.detect_hardware_caps(game_dir, &g_caps);
        }
        const int neural_mode = Gow3Settings::Get().neural_assist_mode.load(std::memory_order_relaxed);
        if (g_api.neural_init) {
            g_api.neural_init(MapNeuralMode(neural_mode));
        }
        g_applied_neural_mode.store(neural_mode, std::memory_order_relaxed);

        const bool low_end = ComputeLowEndActive();
        g_low_end_active.store(low_end, std::memory_order_relaxed);
        g_applied_low_end_mode.store(
            Gow3Settings::Get().low_end_mode.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        const uint32_t budget_mb = ConfiguredStreamingBudgetMb();
        const SpmStorageType tier = EffectiveStorageTier();
        const uint32_t workers = WorkersForStorage(g_topology, tier);
        if (g_api.streaming_init) {
            g_api.streaming_init(budget_mb, 3u, workers);
        }
        if (g_api.streaming_set_hysteresis) {
            g_api.streaming_set_hysteresis(low_end ? 16u : 8u);
        }
        g_applied_storage_mode.store(
            Gow3Settings::Get().storage_optimization.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        g_applied_stream_budget_mb.store(budget_mb, std::memory_order_relaxed);
        g_upload_quota_bytes.store(UploadQuotaForState(tier, false), std::memory_order_relaxed);

        std::printf("SPMCore: active (%up/%ut%s, storage=%s %.0f MB/s, stream_budget=%u MiB, npu_hw=%d)\n",
                    g_topology.physicalCores, g_topology.logicalCores,
                    g_topology.isHeterogeneous ? " hybrid" : "",
                    g_api.get_storage_type_name ? g_api.get_storage_type_name(tier) : "unknown",
                    double(g_caps.storageEstimatedBandwidthMBps), budget_mb,
                    int(g_caps.npuSupported));
    } else {
        const uint32_t hw = std::max(1u, std::thread::hardware_concurrency());
        g_topology.logicalCores = hw;
        g_topology.physicalCores = std::max(1u, hw / 2u);
        g_topology.maxWorkerThreads = std::max(1u, hw - 1u);
        g_low_end_active.store(ComputeLowEndActive(), std::memory_order_relaxed);
        g_applied_stream_budget_mb.store(ConfiguredStreamingBudgetMb(), std::memory_order_relaxed);
        g_upload_quota_bytes.store(UploadQuotaForState(SPM_STORAGE_UNKNOWN, false), std::memory_order_relaxed);
        std::printf("SPMCore: library unavailable, running built-in CPU fallback (%ut)\n", hw);
    }

    SpmRuntimeSnapshot snap{};
    FillBaseSnapshot(snap);
    snap.active_workers = WorkersForStorage(g_topology, EffectiveStorageTier());
    RefreshBackendInfoLocked(snap);
    CopyStr(snap.strategy, sizeof(snap.strategy), "Initialized");
    PublishSnapshot(snap);
    g_initialized.store(true, std::memory_order_release);
}

void Shutdown() {
    std::scoped_lock lock{g_lifecycle_mutex};
    if (!g_initialized.load(std::memory_order_relaxed)) {
        return;
    }
    if (g_spm_loaded.load(std::memory_order_relaxed)) {
        if (g_api.streaming_shutdown) {
            g_api.streaming_shutdown();
        }
        if (g_api.neural_shutdown) {
            g_api.neural_shutdown();
        }
        if (g_api.shutdown_timer) {
            g_api.shutdown_timer();
        }
#ifdef _WIN32
        if (g_api.module) {
            FreeLibrary(g_api.module);
        }
#else
        if (g_api.module) {
            dlclose(g_api.module);
        }
#endif
    }
    g_api = {};
    g_spm_loaded.store(false, std::memory_order_release);
    g_initialized.store(false, std::memory_order_release);
}

uint32_t RecommendedCompileWorkers(uint32_t available_threads) {
    const bool low_end = g_low_end_active.load(std::memory_order_relaxed);
    const uint32_t cap = (available_threads >= 10u && !low_end) ? 8u : 6u;
    if (!g_initialized.load(std::memory_order_acquire)) {
        const uint32_t desired =
            available_threads >= 10u ? (available_threads * 2u) / 3u : available_threads / 2u;
        return std::clamp(desired, 1u, cap);
    }
    const uint32_t max_w = g_topology.maxWorkerThreads > 0 ? g_topology.maxWorkerThreads
                                                           : std::max(1u, available_threads / 2u);
    // Reserve physical/P-core headroom for gameplay simulation and GPU command submission.
    uint32_t target = g_topology.isHeterogeneous
                          ? std::clamp((g_topology.physicalCores + 1u) / 2u, 1u, cap)
                          : std::clamp(std::min(available_threads, max_w) / 2u, 1u, cap);
    if (available_threads >= 10u && !low_end) {
        const uint32_t scale_target = std::max(max_w, (available_threads * 2u) / 3u);
        target = std::max(target, std::clamp(scale_target, 1u, cap));
    }
    if (low_end) {
        return std::min(target, 2u);
    }
    return target;
}

bool ShouldThrottleBackgroundCompile() {
    return g_throttle_bg.load(std::memory_order_relaxed);
}

bool IsLowEndModeActive() {
    return g_low_end_active.load(std::memory_order_relaxed);
}

uint32_t EffectiveUploadQuotaBytesPerFrame() {
    return g_upload_quota_bytes.load(std::memory_order_relaxed);
}

void OnFrameBegin(float frame_time_ms) {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    ApplyLiveSettingsIfNeeded(false);

    const uint64_t frame = g_frame_counter.fetch_add(1, std::memory_order_relaxed);
    const int fps_limit = Gow3Settings::Get().fps_limit.load(std::memory_order_relaxed);
    // Use 16.6667 ms as the strictest frame budget cap when unlimited/high refresh, or the cap period.
    const float limit_ms = fps_limit > 0 ? (1000.0f / static_cast<float>(fps_limit)) : 16.6667f;
    const float target_ms = std::min(16.6667f, std::max(8.3333f, limit_ms));

    if ((frame % 15u) == 0u) {
        const auto perf = Gow3PerfStats::Latest();
        if (perf.p95_ms > 0.0f) {
            g_cached_p95_ms.store(perf.p95_ms, std::memory_order_relaxed);
        }
        if (perf.cpu_percent >= 0.0f) {
            g_cached_cpu_pct.store(perf.cpu_percent, std::memory_order_relaxed);
        }
        const bool thermal_limited =
            perf.gpu_thermal_throttled || (perf.gpu_temp_c >= 87.0f) || (perf.cpu_temp_c >= 90.0f);
        g_cached_thermal_throttle.store(thermal_limited, std::memory_order_relaxed);
    }

    const float clamped_ft = (frame_time_ms > 0.1f && frame_time_ms < 500.0f) ? frame_time_ms : target_ms;
    SpmSchedulerDecision decision{};
    if (g_spm_loaded.load(std::memory_order_relaxed) && g_api.evaluate_scheduler) {
        SpmFrameTelemetry telemetry{};
        telemetry.currentFrameTimeMs = clamped_ft;
        telemetry.targetFrameTimeMs = target_ms;
        telemetry.cpuUsagePercent = g_cached_cpu_pct.load(std::memory_order_relaxed);
        telemetry.p95FrameTimeMs = g_cached_p95_ms.load(std::memory_order_relaxed);
        telemetry.droppedFramesCount = clamped_ft > (target_ms * 1.25f) ? 1u : 0u;
        g_api.evaluate_scheduler(&g_topology, &telemetry, &decision);
    } else {
        const bool overload = clamped_ft > (target_ms * 1.15f) ||
                              g_cached_p95_ms.load(std::memory_order_relaxed) > (target_ms * 1.15f);
        decision.isCriticalLoad = overload ? 1u : 0u;
        decision.throttleBackgroundWork = overload ? 1u : 0u;
        decision.activeWorkerThreads = overload ? 1u : std::clamp(g_topology.maxWorkerThreads, 1u, 4u);
        decision.maxStreamingSliceMs = overload ? 1u : 2u;
        CopyStr(decision.strategyDescription, sizeof(decision.strategyDescription),
                overload ? "Built-in CPU fallback: critical load throttle" : "Built-in CPU fallback: headroom");
    }

    const bool thermal_throttle = g_cached_thermal_throttle.load(std::memory_order_relaxed);
    const bool throttle = (decision.throttleBackgroundWork != 0) || thermal_throttle;
    if (thermal_throttle) {
        decision.throttleBackgroundWork = 1u;
        CopyStr(decision.strategyDescription, sizeof(decision.strategyDescription),
                "Thermal headroom throttle: background work reduced");
    }
    g_throttle_bg.store(throttle, std::memory_order_relaxed);
    const SpmStorageType tier = EffectiveStorageTier();
    const uint32_t quota = UploadQuotaForState(tier, throttle);
    g_upload_quota_bytes.store(quota, std::memory_order_relaxed);

    if (g_spm_loaded.load(std::memory_order_relaxed)) {
        if (g_api.streaming_process_slice) {
            g_api.streaming_process_slice(quota);
        }
        if (g_api.streaming_enforce_budget) {
            g_api.streaming_enforce_budget();
        }
    }

    if ((frame % 15u) == 0u) {
        SpmRuntimeSnapshot snap{};
        FillBaseSnapshot(snap);
        snap.critical_load = decision.isCriticalLoad;
        snap.throttle_bg = decision.throttleBackgroundWork;
        snap.active_workers = decision.activeWorkerThreads;
        snap.max_streaming_slice_ms = decision.maxStreamingSliceMs;
        CopyStr(snap.strategy, sizeof(snap.strategy), decision.strategyDescription);

        if (g_spm_loaded.load(std::memory_order_relaxed) && g_api.neural_predict) {
            SpmPredictorInput in{};
            in.fovDegrees = 70.0f;
            in.currentVramUsedMb = snap.current_vram_used_mb;
            in.currentVramBudgetMb =
                snap.current_vram_budget_mb > 0 ? snap.current_vram_budget_mb : snap.streaming_budget_mb;
            SpmPredictorOutput out{};
            if (g_api.neural_predict(&in, &out) != 0) {
                snap.lookahead_meters = out.recommendedLookaheadDistance;
                snap.proactive_lod_bias = out.proactiveLodBias;
                snap.predicted_vram_mb = out.predictedShortTermVramMb;
            }
        }
        if (g_spm_loaded.load(std::memory_order_relaxed) && g_api.streaming_get_stats) {
            SpmStreamingStats stats{};
            g_api.streaming_get_stats(&stats);
            snap.direct_storage_dll_found = snap.direct_storage_dll_found || (stats.directStorageAvailable != 0);
        }
        RefreshBackendInfoLocked(snap);
        PublishSnapshot(snap);
    }
}

void UpdateVramTelemetry(uint32_t used_mb, uint32_t budget_mb, uint32_t os_available_mb,
                         bool is_integrated) {
    g_vram_used_mb.store(used_mb, std::memory_order_relaxed);
    g_vram_budget_mb.store(budget_mb, std::memory_order_relaxed);
    if (g_initialized.load(std::memory_order_acquire) && g_spm_loaded.load(std::memory_order_relaxed) &&
        g_api.streaming_update_gpu_budget) {
        g_api.streaming_update_gpu_budget(budget_mb, os_available_mb, is_integrated ? 1 : 0);
    }
}

void UpdateGpuFenceTelemetry(uint64_t submitted_fence, uint64_t completed_fence) {
    if (g_initialized.load(std::memory_order_acquire) && g_spm_loaded.load(std::memory_order_relaxed) &&
        g_api.streaming_set_gpu_fence) {
        g_api.streaming_set_gpu_fence(submitted_fence, completed_fence);
    }
}

void SyncLiveSettings() {
    ApplyLiveSettingsIfNeeded(false);
}

void GetSnapshot(SpmRuntimeSnapshot& out) {
    for (;;) {
        const uint64_t seq1 = g_snapshot_seq.load(std::memory_order_acquire);
        if ((seq1 & 1u) != 0u) {
            continue;
        }
        out = g_snapshot;
        const uint64_t seq2 = g_snapshot_seq.load(std::memory_order_acquire);
        if (seq1 == seq2) {
            return;
        }
    }
}

} // namespace Gow3SpmBridge
