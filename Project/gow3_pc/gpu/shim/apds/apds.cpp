// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Orchestrator Implementation
#include "apds.h"
#include "apds_cpu.h"
#include "apds_crash_watchdog.h"
#include "apds_frame.h"
#include "apds_gpu.h"
#include "apds_memory.h"
#include "apds_ring_buffer.h"
#include "apds_time.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

namespace APDS {

namespace fs = std::filesystem;

class Manager {
public:
    static Manager& Instance() {
        static Manager inst;
        return inst;
    }

    void Init(TelemetryMode requested_mode, const std::string& custom_session_name) {
        std::lock_guard<std::mutex> lock(init_mutex);
        if (initialized) return;

        mode = requested_mode;

        // Check environment overrides
        if (const char* env_en = std::getenv("GOW3_TELEMETRY_ENABLED")) {
            if (std::string(env_en) == "0" || std::string(env_en) == "false") {
                mode = TelemetryMode::Disabled;
            }
        }
        if (const char* env_prof = std::getenv("GOW3_TELEMETRY_PROFILE")) {
            std::string p(env_prof);
            if (p == "LIGHT") mode = TelemetryMode::Light;
            else if (p == "STANDARD") mode = TelemetryMode::Standard;
            else if (p == "DEEP") mode = TelemetryMode::Deep;
            else if (p == "FORENSIC") mode = TelemetryMode::Forensic;
            else if (p == "DISABLED") mode = TelemetryMode::Disabled;
        }

        if (mode == TelemetryMode::Disabled) {
            initialized = true;
            return;
        }

        start_time_us = NowUs();
        start_time_iso = FormatIso8601Utc(start_time_us);

        // Session directory naming
        std::string date_str = FormatTimestampForPath(start_time_us);
        std::string rand_suffix = custom_session_name.empty() ? "session" : custom_session_name;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "_%04x", static_cast<unsigned int>(start_time_us & 0xFFFF));
        session_dir = "Diagnostics/Sessions/" + date_str + buf;

        try {
            fs::create_directories(session_dir);
        } catch (...) {
            session_dir = "./" + date_str + buf;
            fs::create_directories(session_dir);
        }

        // Initialize sub-collectors
        cpu_collector = std::make_unique<CpuCollector>();
        gpu_collector = std::make_unique<GpuCollector>();
        memory_collector = std::make_unique<MemoryCollector>();
        frame_tracker = std::make_unique<FrameTracker>();
        watchdog = std::make_unique<CrashWatchdog>();

        // Write hardware profile
        WriteHardwareProfile();

        // Initialize CSV file headers
        InitCsvHeaders();

        // Start Crash & Hang Watchdog
        watchdog->Initialize(session_dir, [this]() { FlushBuffers(); });

        // Start background writer thread
        writer_running = true;
        writer_thread = std::thread(&Manager::WriterWorker, this);

        initialized = true;

        RecordEvent(EventCategory::Info, EventSeverity::Info, "APDS_START",
                    "Advanced Performance & Diagnostics System initialized in mode " + std::string(ToString(mode)),
                    "{\"session_dir\":\"" + session_dir + "\"}");
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(init_mutex);
        if (!initialized || mode == TelemetryMode::Disabled) return;

        RecordEvent(EventCategory::Info, EventSeverity::Info, "APDS_STOP",
                    "Shutting down Advanced Performance & Diagnostics System", "{}");

        if (watchdog) {
            watchdog->Shutdown();
        }

        writer_running = false;
        if (writer_thread.joinable()) {
            writer_thread.join();
        }

        FlushBuffers();
        WriteSessionManifest();
        LaunchReportGenerator();

        initialized = false;
    }

    bool IsActive() const noexcept {
        return initialized && mode != TelemetryMode::Disabled;
    }

    TelemetryMode GetCurrentMode() const noexcept {
        return mode;
    }

    const std::string& GetSessionDir() const noexcept {
        return session_dir;
    }

    void OnFrameBegin(uint64_t frame_index) {
        if (!IsActive()) return;
        frame_tracker->OnFrameBegin(frame_index);
        if (watchdog) watchdog->Heartbeat();
    }

    void OnFramePresent(uint64_t frame_index, uint32_t draw_calls, uint32_t compute_dispatches) {
        if (!IsActive()) return;
        FrameSample sample{};
        StutterIncident stutter{};
        if (frame_tracker->OnFramePresent(frame_index, draw_calls, compute_dispatches, sample, &stutter)) {
            ring_frames.Push(sample);
            if (sample.is_stutter) {
                ring_stutters.Push(stutter);
            }
        }
        if (watchdog) watchdog->Heartbeat();
    }

    void OnResourceUpload(const std::string& type, const std::string& id, uint64_t size_bytes,
                          uint32_t mips, const std::string& details) {
        if (!IsActive()) return;
        ResourceEvent re{};
        re.timestamp_us = NowUs();
        re.frame_index = current_frame.load();
        re.action = "UPLOAD";
        re.resource_type = type;
        re.identifier = id;
        re.size_bytes = size_bytes;
        re.mips = mips;
        re.details = details;
        ring_resources.Push(re);
    }

    void OnResourceEvict(const std::string& type, const std::string& id, uint64_t size_bytes,
                         const std::string& details) {
        if (!IsActive()) return;
        ResourceEvent re{};
        re.timestamp_us = NowUs();
        re.frame_index = current_frame.load();
        re.action = "EVICT";
        re.resource_type = type;
        re.identifier = id;
        re.size_bytes = size_bytes;
        re.details = details;
        ring_resources.Push(re);
    }

    void OnEvent(EventCategory category, EventSeverity severity, const std::string& code,
                 const std::string& message, const std::string& context_json) {
        if (!IsActive()) return;
        StructuredEvent ev{};
        ev.timestamp_us = NowUs();
        ev.timestamp_iso = FormatIso8601Utc(ev.timestamp_us);
        ev.frame_index = current_frame.load();
#ifdef _WIN32
        ev.thread_id = GetCurrentThreadId();
#endif
        ev.category = category;
        ev.severity = severity;
        ev.code = code;
        ev.message = message;
        ev.context_json = context_json.empty() ? "{}" : context_json;
        ring_events.Push(ev);
    }

    void OnIo(const std::string& op, const std::string& path, uint64_t bytes, double duration_us, bool success) {
        if (!IsActive()) return;
        IoEvent ie{};
        ie.timestamp_us = NowUs();
        ie.frame_index = current_frame.load();
        ie.operation = op;
        ie.path = path;
        ie.bytes = bytes;
        ie.duration_us = duration_us;
        ie.success = success;
        ring_io.Push(ie);
    }

    void FlushBuffers() {
        // Drain frames
        {
            std::ofstream out(session_dir + "/frame_times.csv", std::ios::app);
            FrameSample s;
            while (ring_frames.Pop(s)) {
                out << s.frame_index << ","
                    << s.present_time_us << ","
                    << std::fixed << std::setprecision(3) << s.frame_time_ms << ","
                    << s.cpu_time_ms << ","
                    << s.gpu_time_ms << ","
                    << s.jitter_ms << ","
                    << s.draw_calls << ","
                    << s.compute_dispatches << ","
                    << (s.is_stutter ? 1 : 0) << "\n";
            }
        }

        // Drain CPU
        {
            std::ofstream out(session_dir + "/cpu_samples.csv", std::ios::app);
            CpuSample s;
            while (ring_cpu.Pop(s)) {
                out << s.timestamp_us << ","
                    << std::fixed << std::setprecision(2) << s.total_cpu_percent << ","
                    << s.process_cpu_percent << ","
                    << s.cpu_temp_c;
                for (float c : s.per_core_percent) {
                    out << "," << c;
                }
                out << "\n";
            }
        }

        // Drain GPU
        {
            std::ofstream out(session_dir + "/gpu_samples.csv", std::ios::app);
            GpuSample s;
            while (ring_gpu.Pop(s)) {
                out << s.timestamp_us << ","
                    << std::fixed << std::setprecision(2) << s.gpu_engine_3d_percent << ","
                    << s.gpu_temp_c << ","
                    << s.core_clock_mhz << ","
                    << s.mem_clock_mhz << ","
                    << s.power_watts << ","
                    << s.power_limit_watts << ","
                    << s.vram_local_used_bytes << ","
                    << s.vram_local_budget_bytes << ","
                    << s.vram_non_local_used_bytes << "\n";
            }
        }

        // Drain Memory
        {
            std::ofstream out(session_dir + "/memory_samples.csv", std::ios::app);
            MemorySample s;
            while (ring_memory.Pop(s)) {
                out << s.timestamp_us << ","
                    << s.process_working_set_bytes << ","
                    << s.process_peak_working_set_bytes << ","
                    << s.process_private_commit_bytes << ","
                    << s.system_available_ram_bytes << ","
                    << s.system_memory_load_percent << "\n";
            }
        }

        // Drain Resources
        {
            std::ofstream out(session_dir + "/resources.jsonl", std::ios::app);
            ResourceEvent r;
            while (ring_resources.Pop(r)) {
                out << "{\"timestamp_us\":" << r.timestamp_us
                    << ",\"frame\":" << r.frame_index
                    << ",\"action\":\"" << r.action << "\""
                    << ",\"type\":\"" << r.resource_type << "\""
                    << ",\"id\":\"" << EscapeJson(r.identifier) << "\""
                    << ",\"bytes\":" << r.size_bytes
                    << ",\"mips\":" << r.mips
                    << ",\"details\":\"" << EscapeJson(r.details) << "\"}\n";
            }
        }

        // Drain Events
        {
            std::ofstream out(session_dir + "/application_events.jsonl", std::ios::app);
            StructuredEvent e;
            while (ring_events.Pop(e)) {
                out << "{\"timestamp_us\":" << e.timestamp_us
                    << ",\"timestamp_iso\":\"" << e.timestamp_iso << "\""
                    << ",\"frame\":" << e.frame_index
                    << ",\"thread_id\":" << e.thread_id
                    << ",\"category\":\"" << ToString(e.category) << "\""
                    << ",\"severity\":\"" << ToString(e.severity) << "\""
                    << ",\"code\":\"" << EscapeJson(e.code) << "\""
                    << ",\"message\":\"" << EscapeJson(e.message) << "\""
                    << ",\"context\":" << e.context_json << "}\n";
            }
        }

        // Drain IO
        {
            std::ofstream out(session_dir + "/io_events.jsonl", std::ios::app);
            IoEvent io;
            while (ring_io.Pop(io)) {
                out << "{\"timestamp_us\":" << io.timestamp_us
                    << ",\"frame\":" << io.frame_index
                    << ",\"op\":\"" << io.operation << "\""
                    << ",\"path\":\"" << EscapeJson(io.path) << "\""
                    << ",\"bytes\":" << io.bytes
                    << ",\"duration_us\":" << std::fixed << std::setprecision(1) << io.duration_us
                    << ",\"success\":" << (io.success ? "true" : "false") << "}\n";
            }
        }

        // Drain Stutters
        {
            std::ofstream out(session_dir + "/stutters.jsonl", std::ios::app);
            StutterIncident st;
            while (ring_stutters.Pop(st)) {
                out << "{\"frame\":" << st.frame_index
                    << ",\"timestamp_us\":" << st.timestamp_us
                    << ",\"frame_time_ms\":" << std::fixed << std::setprecision(2) << st.frame_time_ms
                    << ",\"baseline_ms\":" << st.baseline_ms
                    << ",\"primary_suspicion\":\"" << st.primary_suspicion << "\""
                    << ",\"details\":\"" << EscapeJson(st.details) << "\"}\n";
            }
        }
    }

private:
    Manager() = default;
    ~Manager() { Stop(); }

    static std::string EscapeJson(const std::string& s) {
        std::string res;
        for (char c : s) {
            if (c == '"') res += "\\\"";
            else if (c == '\\') res += "\\\\";
            else if (c == '\n') res += "\\n";
            else if (c == '\r') res += "\\r";
            else if (c == '\t') res += "\\t";
            else res += c;
        }
        return res;
    }

    void InitCsvHeaders() {
        {
            std::ofstream out(session_dir + "/frame_times.csv");
            out << "frame_index,timestamp_us,frame_time_ms,cpu_time_ms,gpu_time_ms,jitter_ms,draw_calls,compute_dispatches,is_stutter\n";
        }
        {
            std::ofstream out(session_dir + "/cpu_samples.csv");
            out << "timestamp_us,total_cpu_percent,process_cpu_percent,cpu_temp_c";
            for (size_t i = 0; i < hardware.cpu.logical_processors; ++i) {
                out << ",core_" << i << "_percent";
            }
            out << "\n";
        }
        {
            std::ofstream out(session_dir + "/gpu_samples.csv");
            out << "timestamp_us,gpu_engine_3d_percent,gpu_temp_c,core_clock_mhz,mem_clock_mhz,power_watts,power_limit_watts,vram_local_used_bytes,vram_local_budget_bytes,vram_non_local_used_bytes\n";
        }
        {
            std::ofstream out(session_dir + "/memory_samples.csv");
            out << "timestamp_us,process_working_set_bytes,process_peak_working_set_bytes,process_private_commit_bytes,system_available_ram_bytes,system_memory_load_percent\n";
        }
    }

    void WriteHardwareProfile() {
        hardware.cpu = cpu_collector->GetTopology();
        hardware.primary_gpu = gpu_collector->DetectGpuInfo();
        hardware.total_ram_bytes = memory_collector->GetTotalSystemRamBytes();
#ifdef _WIN32
        hardware.os_version = "Windows 11 / Windows 10 (NT 10.0)";
#else
        hardware.os_version = "POSIX / Linux";
#endif

        std::ofstream out(session_dir + "/hardware_profile.json");
        if (out.is_open()) {
            out << "{\n";
            out << "  \"cpu\": {\n";
            out << "    \"vendor\": \"" << hardware.cpu.vendor << "\",\n";
            out << "    \"brand\": \"" << EscapeJson(hardware.cpu.brand) << "\",\n";
            out << "    \"physical_cores\": " << hardware.cpu.physical_cores << ",\n";
            out << "    \"logical_processors\": " << hardware.cpu.logical_processors << ",\n";
            out << "    \"l1_cache_bytes\": " << hardware.cpu.l1_cache_bytes << ",\n";
            out << "    \"l2_cache_bytes\": " << hardware.cpu.l2_cache_bytes << ",\n";
            out << "    \"l3_cache_bytes\": " << hardware.cpu.l3_cache_bytes << ",\n";
            out << "    \"has_avx2\": " << (hardware.cpu.has_avx2 ? "true" : "false") << ",\n";
            out << "    \"has_avx512\": " << (hardware.cpu.has_avx512 ? "true" : "false") << "\n";
            out << "  },\n";
            out << "  \"gpu\": {\n";
            out << "    \"adapter_name\": \"" << EscapeJson(hardware.primary_gpu.adapter_name) << "\",\n";
            out << "    \"vendor_id\": " << hardware.primary_gpu.vendor_id << ",\n";
            out << "    \"device_id\": " << hardware.primary_gpu.device_id << ",\n";
            out << "    \"dedicated_vram_bytes\": " << hardware.primary_gpu.dedicated_vram_bytes << ",\n";
            out << "    \"shared_vram_bytes\": " << hardware.primary_gpu.shared_vram_bytes << ",\n";
            out << "    \"is_discrete\": " << (hardware.primary_gpu.is_discrete ? "true" : "false") << ",\n";
            out << "    \"is_nvml_supported\": " << (hardware.primary_gpu.is_nvml_supported ? "true" : "false") << "\n";
            out << "  },\n";
            out << "  \"total_ram_bytes\": " << hardware.total_ram_bytes << ",\n";
            out << "  \"os_version\": \"" << hardware.os_version << "\"\n";
            out << "}\n";
        }
    }

    void WriteSessionManifest() {
        int64_t end_time_us = NowUs();
        FrameStatsSummary fs = frame_tracker->CalculateSummary();

        std::ofstream out(session_dir + "/session_manifest.json");
        if (out.is_open()) {
            out << "{\n";
            out << "  \"session_id\": \"" << fs::path(session_dir).filename().string() << "\",\n";
            out << "  \"telemetry_mode\": \"" << ToString(mode) << "\",\n";
            out << "  \"start_time_iso\": \"" << start_time_iso << "\",\n";
            out << "  \"end_time_iso\": \"" << FormatIso8601Utc(end_time_us) << "\",\n";
            out << "  \"duration_seconds\": " << std::fixed << std::setprecision(2) << fs.duration_seconds << ",\n";
            out << "  \"total_frames\": " << fs.total_frames << ",\n";
            out << "  \"avg_fps\": " << fs.avg_fps << ",\n";
            out << "  \"min_fps\": " << fs.min_fps << ",\n";
            out << "  \"max_fps\": " << fs.max_fps << ",\n";
            out << "  \"low_1pct_fps\": " << fs.low_1pct_fps << ",\n";
            out << "  \"low_01pct_fps\": " << fs.low_01pct_fps << ",\n";
            out << "  \"p50_frame_time_ms\": " << fs.p50_ms << ",\n";
            out << "  \"p95_frame_time_ms\": " << fs.p95_ms << ",\n";
            out << "  \"p99_frame_time_ms\": " << fs.p99_ms << ",\n";
            out << "  \"avg_jitter_ms\": " << fs.avg_jitter_ms << ",\n";
            out << "  \"total_stutters\": " << fs.total_stutters << ",\n";
            out << "  \"clean_exit\": true\n";
            out << "}\n";
        }
    }

    void LaunchReportGenerator() {
#ifdef _WIN32
        std::string script_path = "Diagnostics/Tools/report_generator.py";
        if (fs::exists(script_path)) {
            std::string cmd = "python \"" + script_path + "\" --session \"" + session_dir + "\"";
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
#endif
    }

    void WriterWorker() {
        int sleep_ms = 500;
        if (mode == TelemetryMode::Light) sleep_ms = 1000;
        else if (mode == TelemetryMode::Deep) sleep_ms = 100;
        else if (mode == TelemetryMode::Forensic) sleep_ms = 50;

        auto last_sample_time = std::chrono::steady_clock::now();

        while (writer_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_sample_time).count();

            if (elapsed >= sleep_ms) {
                last_sample_time = now;
                // Sample hardware
                if (cpu_collector) {
                    ring_cpu.Push(cpu_collector->Sample(true));
                }

                GpuSample gpu_s{};
                if (gpu_collector && gpu_collector->Sample(gpu_s)) {
                    ring_gpu.Push(gpu_s);
                }

                MemorySample mem_s{};
                if (memory_collector && memory_collector->Sample(mem_s)) {
                    ring_memory.Push(mem_s);
                }

                FlushBuffers();
            }
        }
    }

    std::mutex init_mutex;
    bool initialized{false};
    TelemetryMode mode{TelemetryMode::Standard};
    std::string session_dir;
    int64_t start_time_us{0};
    std::string start_time_iso;
    HardwareProfile hardware;

    std::atomic<uint64_t> current_frame{0};

    std::unique_ptr<CpuCollector> cpu_collector;
    std::unique_ptr<GpuCollector> gpu_collector;
    std::unique_ptr<MemoryCollector> memory_collector;
    std::unique_ptr<FrameTracker> frame_tracker;
    std::unique_ptr<CrashWatchdog> watchdog;

    RingBuffer<FrameSample> ring_frames{4096};
    RingBuffer<CpuSample> ring_cpu{256};
    RingBuffer<GpuSample> ring_gpu{256};
    RingBuffer<MemorySample> ring_memory{256};
    RingBuffer<ResourceEvent> ring_resources{4096};
    RingBuffer<StructuredEvent> ring_events{8192};
    RingBuffer<IoEvent> ring_io{2048};
    RingBuffer<StutterIncident> ring_stutters{1024};

    std::atomic<bool> writer_running{false};
    std::thread writer_thread;
};

// Global public wrapper functions
void Initialize(TelemetryMode mode, const std::string& session_name) {
    Manager::Instance().Init(mode, session_name);
}

void Shutdown() {
    Manager::Instance().Stop();
}

bool IsEnabled() noexcept {
    return Manager::Instance().IsActive();
}

TelemetryMode GetMode() noexcept {
    return Manager::Instance().GetCurrentMode();
}

const std::string& GetSessionDirectory() noexcept {
    return Manager::Instance().GetSessionDir();
}

void RecordFrameBegin(uint64_t frame_index) {
    Manager::Instance().OnFrameBegin(frame_index);
}

void RecordFramePresent(uint64_t frame_index, uint32_t draw_calls, uint32_t compute_dispatches) {
    Manager::Instance().OnFramePresent(frame_index, draw_calls, compute_dispatches);
}

void RecordResourceUpload(const std::string& type, const std::string& id, uint64_t size_bytes,
                          uint32_t mips, const std::string& details) {
    Manager::Instance().OnResourceUpload(type, id, size_bytes, mips, details);
}

void RecordResourceEvict(const std::string& type, const std::string& id, uint64_t size_bytes,
                         const std::string& details) {
    Manager::Instance().OnResourceEvict(type, id, size_bytes, details);
}

void RecordEvent(EventCategory category, EventSeverity severity, const std::string& code,
                 const std::string& message, const std::string& context_json) {
    Manager::Instance().OnEvent(category, severity, code, message, context_json);
}

void RecordIo(const std::string& op, const std::string& path, uint64_t bytes, double duration_us,
              bool success) {
    Manager::Instance().OnIo(op, path, bytes, duration_us, success);
}

void Flush() {
    Manager::Instance().FlushBuffers();
}

} // namespace APDS
