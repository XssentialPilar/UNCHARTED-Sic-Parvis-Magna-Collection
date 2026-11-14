// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Memory Monitor
#pragma once

#include "apds_types.h"

namespace APDS {

class MemoryCollector {
public:
    MemoryCollector();
    ~MemoryCollector();

    MemoryCollector(const MemoryCollector&) = delete;
    MemoryCollector& operator=(const MemoryCollector&) = delete;

    uint64_t GetTotalSystemRamBytes() const noexcept;
    bool Sample(MemorySample& out_sample);
};

} // namespace APDS
