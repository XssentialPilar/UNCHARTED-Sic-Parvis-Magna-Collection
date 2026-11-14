// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - GPU Telemetry Collector
#pragma once

#include "apds_types.h"

namespace APDS {

class GpuCollector {
public:
    GpuCollector();
    ~GpuCollector();

    GpuCollector(const GpuCollector&) = delete;
    GpuCollector& operator=(const GpuCollector&) = delete;

    GpuInfo DetectGpuInfo();
    bool Sample(GpuSample& out_sample);

private:
    struct Impl;
    Impl* impl = nullptr;
};

} // namespace APDS
