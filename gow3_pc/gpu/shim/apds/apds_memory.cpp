// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Memory Monitor
#include "apds_memory.h"
#include "apds_time.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace APDS {

MemoryCollector::MemoryCollector() = default;
MemoryCollector::~MemoryCollector() = default;

uint64_t MemoryCollector::GetTotalSystemRamBytes() const noexcept {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPhys;
    }
#endif
    return 0;
}

bool MemoryCollector::Sample(MemorySample& out_sample) {
    out_sample = {};
    out_sample.timestamp_us = NowUs();
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        out_sample.system_available_ram_bytes = status.ullAvailPhys;
        out_sample.system_memory_load_percent = status.dwMemoryLoad;
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        out_sample.process_working_set_bytes = pmc.WorkingSetSize;
        out_sample.process_peak_working_set_bytes = pmc.PeakWorkingSetSize;
        out_sample.process_private_commit_bytes = pmc.PrivateUsage;
    }
    return true;
#else
    return false;
#endif
}

} // namespace APDS
