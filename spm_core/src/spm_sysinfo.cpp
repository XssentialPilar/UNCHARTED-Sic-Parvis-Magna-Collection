// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Real CPU topology queries (physical cores, SMT, hybrid topologies).
#include "spm_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#endif

namespace spm {

#ifdef _WIN32

namespace {

uint32_t CountLogicalProcessors() {
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        count = info.dwNumberOfProcessors;
    }
    return count > 0 ? static_cast<uint32_t>(count) : 1u;
}

} // namespace

CpuTopologyInfo QueryCpuTopology() {
    CpuTopologyInfo result{};
    result.logicalCores = CountLogicalProcessors();

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        // The API is unavailable: logical cores are the only safe lower bound.
        result.physicalCores = result.logicalCores;
        return result;
    }

    std::vector<unsigned char> buffer(length);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length)) {
        result.physicalCores = result.logicalCores;
        return result;
    }

    uint32_t physical = 0;
    bool efficiencyClassSeen[256]{};
    uint32_t efficiencyClasses = 0;

    size_t offset = 0;
    // Each record starts with Relationship + Size (8 bytes); sizeof the whole
    // union is larger than some records (e.g. PROCESSOR_RELATIONSHIP), so it
    // must not be used as the loop bound or the last core is skipped.
    while (offset + 2 * sizeof(DWORD) <= length) {
        const auto* info =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (info->Size < 2 * sizeof(DWORD) || offset + info->Size > length) {
            break;
        }
        if (info->Relationship == RelationProcessorCore) {
            ++physical;
            const BYTE efficiency = info->Processor.EfficiencyClass;
            if (!efficiencyClassSeen[efficiency]) {
                efficiencyClassSeen[efficiency] = true;
                ++efficiencyClasses;
            }
        }
        offset += info->Size;
    }

    // Heterogeneous only when the firmware actually reports distinct classes.
    result.heterogeneous = efficiencyClasses > 1;
    result.physicalCores = physical > 0 ? physical : result.logicalCores;
    if (result.physicalCores > result.logicalCores) {
        result.physicalCores = result.logicalCores;
    }
    return result;
}

#else // POSIX / Linux

namespace {

bool ReadUintFile(const std::filesystem::path& path, long& value) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    long parsed = 0;
    if (!(file >> parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

} // namespace

CpuTopologyInfo QueryCpuTopology() {
    CpuTopologyInfo result{};
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) {
        online = sysconf(_SC_NPROCESSORS_CONF);
    }
    result.logicalCores = online > 0 ? static_cast<uint32_t>(online) : 1u;

    // Count unique (physical package, core id) pairs. This is correct for SMT
    // (several logical CPUs share one core) and for multi-socket machines.
    std::set<std::pair<long, long>> cores;
    std::set<long> capacities;
    std::error_code ec;
    const std::filesystem::path cpuRoot("/sys/devices/system/cpu");

    if (std::filesystem::is_directory(cpuRoot, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(cpuRoot, ec)) {
            if (ec) {
                break;
            }
            const std::string name = entry.path().filename().string();
            if (name.size() < 4 || name.compare(0, 3, "cpu") != 0) {
                continue;
            }
            if (!std::all_of(name.begin() + 3, name.end(),
                             [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                continue;
            }

            long coreId = -1;
            long packageId = 0;
            if (!ReadUintFile(entry.path() / "topology" / "core_id", coreId)) {
                continue;
            }
            if (!ReadUintFile(entry.path() / "topology" / "physical_package_id", packageId)) {
                packageId = 0;
            }
            cores.emplace(packageId, coreId);

            long capacity = -1;
            if (ReadUintFile(entry.path() / "cpu_capacity", capacity)) {
                capacities.insert(capacity);
            }
        }
    }

    if (!cores.empty()) {
        result.physicalCores = static_cast<uint32_t>(cores.size());
    } else {
        // No sysfs topology (containers, old kernels): do not invent SMT.
        result.physicalCores = result.logicalCores;
    }
    if (result.physicalCores == 0 || result.physicalCores > result.logicalCores) {
        result.physicalCores = result.logicalCores;
    }

    // big.LITTLE / hybrid cores expose different capacity values.
    result.heterogeneous = capacities.size() > 1;
    return result;
}

#endif

} // namespace spm
