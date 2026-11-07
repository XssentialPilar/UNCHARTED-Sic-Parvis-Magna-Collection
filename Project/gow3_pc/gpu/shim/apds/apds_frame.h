// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Frame Performance Analyzer
#pragma once

#include "apds_types.h"
#include <mutex>
#include <vector>

namespace APDS {

struct FrameStatsSummary {
    uint64_t total_frames{0};
    double duration_seconds{0.0};
    double avg_fps{0.0};
    double min_fps{0.0};
    double max_fps{0.0};
    double p50_ms{0.0};
    double p95_ms{0.0};
    double p99_ms{0.0};
    double low_1pct_fps{0.0};
    double low_01pct_fps{0.0};
    double avg_jitter_ms{0.0};
    uint64_t total_stutters{0};
};

class FrameTracker {
public:
    FrameTracker();
    ~FrameTracker();

    FrameTracker(const FrameTracker&) = delete;
    FrameTracker& operator=(const FrameTracker&) = delete;

    void OnFrameBegin(uint64_t frame_index, int64_t timestamp_us = 0);
    bool OnFramePresent(uint64_t frame_index, uint32_t draw_calls, uint32_t compute_dispatches,
                        FrameSample& out_sample, StutterIncident* out_stutter = nullptr,
                        int64_t timestamp_us = 0);

    FrameStatsSummary CalculateSummary() const;
    void Reset();

private:
    int64_t last_frame_begin_us{0};
    int64_t last_present_time_us{0};
    double last_frame_time_ms{16.666};
    double ema_frame_time_ms{16.666};
    uint64_t current_frame_index{0};
    std::vector<double> history_frame_times;
    uint64_t stutters_count{0};
    double total_jitter_ms{0.0};
    mutable std::mutex stats_mutex;
};

} // namespace APDS
