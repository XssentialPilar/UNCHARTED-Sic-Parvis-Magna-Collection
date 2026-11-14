// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Crash & Hang Diagnostics
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

struct _EXCEPTION_POINTERS;

namespace APDS {

class CrashWatchdog {
public:
    using FlushCallback = std::function<void()>;

    CrashWatchdog();
    ~CrashWatchdog();

    CrashWatchdog(const CrashWatchdog&) = delete;
    CrashWatchdog& operator=(const CrashWatchdog&) = delete;

    void Initialize(const std::string& session_directory, FlushCallback on_flush);
    void Shutdown();

    void Heartbeat();
    void SetEnabled(bool enabled);

private:
    void WatchdogLoop();
    static long __stdcall UnhandledExceptionFilter(_EXCEPTION_POINTERS* ep);

    std::string session_dir;
    FlushCallback flush_cb;
    std::atomic<int64_t> last_heartbeat_us{0};
    std::atomic<bool> running{false};
    std::atomic<bool> hang_detected{false};
    std::thread watchdog_thread;
    void* prev_filter{nullptr};
};

} // namespace APDS
