// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Crash & Hang Diagnostics
#include "apds_crash_watchdog.h"
#include "apds_time.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace APDS {

static CrashWatchdog* s_active_watchdog = nullptr;

#ifdef _WIN32

static const char* GetExceptionCodeString(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
        default:                                 return "EXCEPTION_UNKNOWN";
    }
}

long __stdcall CrashWatchdog::UnhandledExceptionFilter(_EXCEPTION_POINTERS* ep) {
    if (!s_active_watchdog) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::string dump_path = s_active_watchdog->session_dir + "/minidump.dmp";
    std::string manifest_path = s_active_watchdog->session_dir + "/crash_manifest.json";

    // 1. Try to write MiniDump via dbghelp.dll
    HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbghelp) {
        typedef BOOL(WINAPI * PFN_MiniDumpWriteDump)(
            HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
            PMINIDUMP_EXCEPTION_INFORMATION,
            PMINIDUMP_USER_STREAM_INFORMATION,
            PMINIDUMP_CALLBACK_INFORMATION);

        auto fn_write_dump = reinterpret_cast<PFN_MiniDumpWriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
        if (fn_write_dump) {
            HANDLE dump_file = CreateFileA(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (dump_file != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mei{};
                mei.ThreadId = GetCurrentThreadId();
                mei.ExceptionPointers = ep;
                mei.ClientPointers = FALSE;

                fn_write_dump(GetCurrentProcess(), GetCurrentProcessId(), dump_file,
                              static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithHandleData | MiniDumpWithUnloadedModules),
                              &mei, nullptr, nullptr);
                CloseHandle(dump_file);
            }
        }
        FreeLibrary(dbghelp);
    }

    // 2. Write crash manifest JSON
    std::ofstream mf(manifest_path);
    if (mf.is_open()) {
        DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
        void* addr = ep ? ep->ExceptionRecord->ExceptionAddress : nullptr;
        std::stringstream ss_addr, ss_code;
        ss_addr << "0x" << std::hex << reinterpret_cast<uintptr_t>(addr);
        ss_code << "0x" << std::hex << code;

        mf << "{\n";
        mf << "  \"timestamp_iso\": \"" << FormatIso8601Utc(NowUs()) << "\",\n";
        mf << "  \"thread_id\": " << GetCurrentThreadId() << ",\n";
        mf << "  \"exception_code\": \"" << ss_code.str() << "\",\n";
        mf << "  \"exception_name\": \"" << GetExceptionCodeString(code) << "\",\n";
        mf << "  \"faulting_address\": \"" << ss_addr.str() << "\",\n";
        mf << "  \"minidump_file\": \"minidump.dmp\"\n";
        mf << "}\n";
        mf.close();
    }

    // 3. Flush pending buffers to disk
    if (s_active_watchdog->flush_cb) {
        s_active_watchdog->flush_cb();
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

CrashWatchdog::CrashWatchdog() = default;

CrashWatchdog::~CrashWatchdog() {
    Shutdown();
}

void CrashWatchdog::Initialize(const std::string& session_directory, FlushCallback on_flush) {
    session_dir = session_directory;
    flush_cb = on_flush;
    s_active_watchdog = this;
    last_heartbeat_us = NowUs();

#ifdef _WIN32
    prev_filter = reinterpret_cast<void*>(SetUnhandledExceptionFilter(UnhandledExceptionFilter));
#endif

    running = true;
    watchdog_thread = std::thread(&CrashWatchdog::WatchdogLoop, this);
}

void CrashWatchdog::Shutdown() {
    running = false;
    if (watchdog_thread.joinable()) {
        watchdog_thread.join();
    }

#ifdef _WIN32
    if (prev_filter) {
        SetUnhandledExceptionFilter(reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(prev_filter));
        prev_filter = nullptr;
    }
#endif

    if (s_active_watchdog == this) {
        s_active_watchdog = nullptr;
    }
}

void CrashWatchdog::Heartbeat() {
    last_heartbeat_us = NowUs();
}

void CrashWatchdog::SetEnabled(bool enabled) {
    if (!enabled && running) {
        Shutdown();
    }
}

void CrashWatchdog::WatchdogLoop() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!running) break;

        int64_t elapsed_us = NowUs() - last_heartbeat_us.load();
        if (elapsed_us > 3000000) { // > 3.0 seconds without present heartbeat
            if (!hang_detected.exchange(true)) {
                // First hang detection trigger
                std::string hang_path = session_dir + "/hang_manifest.json";
                std::ofstream hf(hang_path);
                if (hf.is_open()) {
                    hf << "{\n";
                    hf << "  \"timestamp_iso\": \"" << FormatIso8601Utc(NowUs()) << "\",\n";
                    hf << "  \"stall_duration_seconds\": " << (static_cast<double>(elapsed_us) / 1000000.0) << ",\n";
                    hf << "  \"status\": \"STALL_DETECTED\",\n";
                    hf << "  \"message\": \"Render thread failed to present for > 3.0 seconds\"\n";
                    hf << "}\n";
                    hf.close();
                }

                if (flush_cb) {
                    flush_cb();
                }
            }
        } else {
            hang_detected = false;
        }
    }
}

} // namespace APDS
