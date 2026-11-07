// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - CPU Topology & Telemetry
#pragma once

#include "apds_types.h"
#include <memory>
#include <vector>

namespace APDS {

class CpuCollector {
public:
    CpuCollector();
    ~CpuCollector();

    CpuTopology GetTopology() const noexcept { return m_topology; }
    CpuSample Sample(bool collect_per_core = true);

private:
    void DetectTopology();
    void InitPdh();

    CpuTopology m_topology;

#ifdef _WIN32
    void* m_pdh_query{nullptr};
    void* m_pdh_counter{nullptr};
    std::vector<unsigned char> m_pdh_buffer;
    uint64_t m_last_process_time{0};
    uint64_t m_last_wall_time{0};
    uint64_t m_last_idle_time{0};
    uint64_t m_last_kernel_time{0};
    uint64_t m_last_user_time{0};
#endif
};

} // namespace APDS
