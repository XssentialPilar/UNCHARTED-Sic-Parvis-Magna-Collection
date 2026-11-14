// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - CPU Collector Implementation
#include "apds_cpu.h"
#include "apds_time.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

namespace APDS {

namespace {

#ifdef _WIN32
uint64_t FileTimeToUint64(const FILETIME& ft) noexcept {
    return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
#endif

void Cpuid(int leaf, int subleaf, int regs[4]) noexcept {
#if defined(_MSC_VER)
    __cpuidex(regs, leaf, subleaf);
#elif defined(__GNUC__) || defined(__clang__)
    __cpuid_count(leaf, subleaf, regs[0], regs[1], regs[2], regs[3]);
#else
    regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
}

} // namespace

CpuCollector::CpuCollector() {
    DetectTopology();
    InitPdh();
}

CpuCollector::~CpuCollector() {
#ifdef _WIN32
    if (m_pdh_query) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_pdh_query));
        m_pdh_query = nullptr;
    }
#endif
}

void CpuCollector::DetectTopology() {
    int regs[4] = {0};

    // 1. Vendor String
    Cpuid(0, 0, regs);
    char vendor_buf[13] = {0};
    std::memcpy(vendor_buf + 0, &regs[1], 4);
    std::memcpy(vendor_buf + 4, &regs[3], 4);
    std::memcpy(vendor_buf + 8, &regs[2], 4);
    m_topology.vendor = vendor_buf;

    // 2. Brand String
    char brand_buf[49] = {0};
    for (int i = 0; i < 3; ++i) {
        Cpuid(0x80000002 + i, 0, regs);
        std::memcpy(brand_buf + i * 16, regs, 16);
    }
    m_topology.brand = brand_buf;
    // Trim leading whitespace
    size_t first = m_topology.brand.find_first_not_of(' ');
    if (first != std::string::npos) {
        m_topology.brand = m_topology.brand.substr(first);
    }

    // 3. Feature Flags
    Cpuid(1, 0, regs);
    m_topology.has_fma = (regs[2] & (1 << 12)) != 0;
    m_topology.has_avx = (regs[2] & (1 << 28)) != 0;

    Cpuid(7, 0, regs);
    m_topology.has_avx2 = (regs[1] & (1 << 5)) != 0;
    m_topology.has_avx512 = (regs[1] & (1 << 16)) != 0;

#ifdef _WIN32
    // 4. Detailed OS Topology via GetLogicalProcessorInformationEx
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
    if (length > 0) {
        std::vector<uint8_t> buffer(length);
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
        if (GetLogicalProcessorInformationEx(RelationAll, info, &length)) {
            DWORD offset = 0;
            uint32_t physical = 0;
            uint32_t logical = 0;
            uint32_t packages = 0;
            uint32_t numa = 0;
            uint64_t l1 = 0, l2 = 0, l3 = 0;

            while (offset < length) {
                auto* current = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
                if (current->Relationship == RelationProcessorCore) {
                    ++physical;
                    for (WORD g = 0; g < current->Processor.GroupCount; ++g) {
                        for (KAFFINITY mask = current->Processor.GroupMask[g].Mask; mask != 0; mask &= mask - 1) {
                            ++logical;
                        }
                    }
                } else if (current->Relationship == RelationProcessorPackage) {
                    ++packages;
                } else if (current->Relationship == RelationNumaNode) {
                    ++numa;
                } else if (current->Relationship == RelationCache) {
                    if (current->Cache.Level == 1) l1 += current->Cache.CacheSize;
                    else if (current->Cache.Level == 2) l2 += current->Cache.CacheSize;
                    else if (current->Cache.Level == 3) l3 += current->Cache.CacheSize;
                }
                offset += current->Size;
            }

            m_topology.physical_cores = physical;
            m_topology.logical_processors = logical;
            m_topology.packages = std::max(1u, packages);
            m_topology.numa_nodes = std::max(1u, numa);
            m_topology.l1_cache_bytes = l1;
            m_topology.l2_cache_bytes = l2;
            m_topology.l3_cache_bytes = l3;
        }
    }
#endif

    if (m_topology.logical_processors == 0) {
#ifdef _WIN32
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        m_topology.logical_processors = sys_info.dwNumberOfProcessors;
        m_topology.physical_cores = sys_info.dwNumberOfProcessors / 2;
#else
        m_topology.logical_processors = 8;
        m_topology.physical_cores = 4;
#endif
    }
}

void CpuCollector::InitPdh() {
#ifdef _WIN32
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS) {
        PDH_HCOUNTER c = nullptr;
        // Collect per-core utilization across all logical processors
        if (PdhAddEnglishCounterW(q, L"\\Processor Information(*)\\% Processor Time", 0, &c) == ERROR_SUCCESS) {
            PdhCollectQueryData(q);
            m_pdh_query = q;
            m_pdh_counter = c;
        } else {
            PdhCloseQuery(q);
        }
    }
#endif
}

CpuSample CpuCollector::Sample(bool collect_per_core) {
    CpuSample s;
    s.timestamp_us = HighResClock::NowUs();

#ifdef _WIN32
    // 1. Process CPU calculation
    FILETIME ft_create, ft_exit, ft_kernel, ft_user;
    if (GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel, &ft_user)) {
        const uint64_t proc_time = FileTimeToUint64(ft_kernel) + FileTimeToUint64(ft_user);
        const uint64_t wall_time = HighResClock::NowNs() / 100ULL; // 100ns units

        if (m_last_wall_time > 0 && wall_time > m_last_wall_time) {
            const uint64_t proc_delta = proc_time - m_last_process_time;
            const uint64_t wall_delta = wall_time - m_last_wall_time;
            const uint32_t c = std::max(1u, m_topology.logical_processors);
            s.process_cpu_percent = std::clamp(
                float((double(proc_delta) * 100.0) / (double(wall_delta) * double(c))),
                0.0f, 100.0f);
        }
        m_last_process_time = proc_time;
        m_last_wall_time = wall_time;
    }

    // 2. Global System CPU calculation
    FILETIME sys_idle, sys_kernel, sys_user;
    if (GetSystemTimes(&sys_idle, &sys_kernel, &sys_user)) {
        const uint64_t idle = FileTimeToUint64(sys_idle);
        const uint64_t kernel = FileTimeToUint64(sys_kernel);
        const uint64_t user = FileTimeToUint64(sys_user);

        if (m_last_kernel_time > 0) {
            const uint64_t delta_idle = idle - m_last_idle_time;
            const uint64_t delta_kernel = kernel - m_last_kernel_time;
            const uint64_t delta_user = user - m_last_user_time;
            const uint64_t total_sys = delta_kernel + delta_user;

            if (total_sys > 0) {
                const uint64_t busy = (total_sys > delta_idle) ? (total_sys - delta_idle) : 0;
                s.total_cpu_percent = std::clamp(float((double(busy) * 100.0) / double(total_sys)), 0.0f, 100.0f);
            }
        }
        m_last_idle_time = idle;
        m_last_kernel_time = kernel;
        m_last_user_time = user;
    }

    // 3. Per-core utilization via PDH
    if (collect_per_core && m_pdh_query && m_pdh_counter) {
        auto q = static_cast<PDH_HQUERY>(m_pdh_query);
        auto c = static_cast<PDH_HCOUNTER>(m_pdh_counter);
        if (PdhCollectQueryData(q) == ERROR_SUCCESS) {
            DWORD size = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr) == PDH_MORE_DATA) {
                m_pdh_buffer.resize(size);
                auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(m_pdh_buffer.data());
                if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) == ERROR_SUCCESS) {
                    s.per_core_percent.reserve(count);
                    for (DWORD i = 0; i < count; ++i) {
                        const wchar_t* name = items[i].szName;
                        // Skip _Total
                        if (name && !wcsstr(name, L"_Total") && items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                            s.per_core_percent.push_back(std::clamp(float(items[i].FmtValue.doubleValue), 0.0f, 100.0f));
                        }
                    }
                }
            }
        }
    }
#endif

    return s;
}

} // namespace APDS
