// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Frame Performance Analyzer
#include "apds_frame.h"
#include "apds_time.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace APDS {

FrameTracker::FrameTracker() = default;
FrameTracker::~FrameTracker() = default;

void FrameTracker::Reset() {
    std::lock_guard<std::mutex> lock(stats_mutex);
    last_frame_begin_us = 0;
    last_present_time_us = 0;
    last_frame_time_ms = 16.666;
    ema_frame_time_ms = 16.666;
    current_frame_index = 0;
    history_frame_times.clear();
    stutters_count = 0;
    total_jitter_ms = 0.0;
}

void FrameTracker::OnFrameBegin(uint64_t frame_index, int64_t timestamp_us) {
    if (timestamp_us == 0) timestamp_us = NowUs();
    last_frame_begin_us = timestamp_us;
    current_frame_index = frame_index;
}

bool FrameTracker::OnFramePresent(uint64_t frame_index, uint32_t draw_calls, uint32_t compute_dispatches,
                                  FrameSample& out_sample, StutterIncident* out_stutter,
                                  int64_t timestamp_us) {
    if (timestamp_us == 0) timestamp_us = NowUs();

    double frame_time_ms = 16.666;
    if (last_present_time_us > 0) {
        int64_t delta_us = timestamp_us - last_present_time_us;
        if (delta_us > 0 && delta_us < 2000000) { // ignore unrealistic jumps > 2s (e.g. paused/loading)
            frame_time_ms = static_cast<double>(delta_us) / 1000.0;
        }
    }

    double cpu_time_ms = 0.0;
    if (last_frame_begin_us > 0 && timestamp_us >= last_frame_begin_us) {
        cpu_time_ms = static_cast<double>(timestamp_us - last_frame_begin_us) / 1000.0;
    }

    double jitter_ms = std::abs(frame_time_ms - last_frame_time_ms);
    ema_frame_time_ms = 0.05 * frame_time_ms + 0.95 * ema_frame_time_ms;

    bool is_stutter = false;
    // Stutter threshold: frame time > 25.0 ms (missed vblank on 60 Hz) and significantly higher than rolling baseline
    if (frame_time_ms > 24.5 && (frame_time_ms > 1.35 * ema_frame_time_ms || frame_time_ms > 33.0)) {
        is_stutter = true;
    }

    out_sample.frame_index = frame_index;
    out_sample.start_time_us = last_frame_begin_us;
    out_sample.present_time_us = timestamp_us;
    out_sample.frame_time_ms = frame_time_ms;
    out_sample.cpu_time_ms = cpu_time_ms;
    out_sample.gpu_time_ms = frame_time_ms; // estimated presentation time
    out_sample.jitter_ms = jitter_ms;
    out_sample.draw_calls = draw_calls;
    out_sample.compute_dispatches = compute_dispatches;
    out_sample.is_stutter = is_stutter;

    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        history_frame_times.push_back(frame_time_ms);
        total_jitter_ms += jitter_ms;
        if (is_stutter) {
            stutters_count++;
            if (out_stutter) {
                out_stutter->frame_index = frame_index;
                out_stutter->timestamp_us = timestamp_us;
                out_stutter->frame_time_ms = frame_time_ms;
                out_stutter->baseline_ms = ema_frame_time_ms;
                if (cpu_time_ms > 20.0) {
                    out_stutter->primary_suspicion = "CPU_BOUND_SPIKE";
                } else if (draw_calls > 3000) {
                    out_stutter->primary_suspicion = "DRAW_CALL_OVERLOAD";
                } else {
                    out_stutter->primary_suspicion = "GPU_VBLANK_MISS";
                }
            }
        }
    }

    last_frame_time_ms = frame_time_ms;
    last_present_time_us = timestamp_us;
    return true;
}

FrameStatsSummary FrameTracker::CalculateSummary() const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    FrameStatsSummary s{};
    s.total_frames = history_frame_times.size();
    if (history_frame_times.empty()) return s;

    double total_ms = std::accumulate(history_frame_times.begin(), history_frame_times.end(), 0.0);
    s.duration_seconds = total_ms / 1000.0;
    s.avg_fps = (total_ms > 0.0) ? (static_cast<double>(s.total_frames) / s.duration_seconds) : 0.0;
    s.avg_jitter_ms = total_jitter_ms / static_cast<double>(s.total_frames);
    s.total_stutters = stutters_count;

    std::vector<double> sorted = history_frame_times;
    std::sort(sorted.begin(), sorted.end());

    double min_ms = sorted.front();
    double max_ms = sorted.back();
    s.max_fps = (min_ms > 0.0) ? (1000.0 / min_ms) : 0.0;
    s.min_fps = (max_ms > 0.0) ? (1000.0 / max_ms) : 0.0;

    auto get_percentile = [&](double p) -> double {
        size_t idx = static_cast<size_t>(std::clamp(p * static_cast<double>(sorted.size() - 1), 0.0, static_cast<double>(sorted.size() - 1)));
        return sorted[idx];
    };

    s.p50_ms = get_percentile(0.50);
    s.p95_ms = get_percentile(0.95);
    s.p99_ms = get_percentile(0.99);

    // 1% Low: Average of worst 1% frames
    size_t count_1pct = std::max<size_t>(1, sorted.size() / 100);
    double sum_worst_1pct = 0.0;
    for (size_t i = sorted.size() - count_1pct; i < sorted.size(); ++i) {
        sum_worst_1pct += sorted[i];
    }
    double avg_worst_1pct_ms = sum_worst_1pct / static_cast<double>(count_1pct);
    s.low_1pct_fps = (avg_worst_1pct_ms > 0.0) ? (1000.0 / avg_worst_1pct_ms) : 0.0;

    // 0.1% Low: Average of worst 0.1% frames
    size_t count_01pct = std::max<size_t>(1, sorted.size() / 1000);
    double sum_worst_01pct = 0.0;
    for (size_t i = sorted.size() - count_01pct; i < sorted.size(); ++i) {
        sum_worst_01pct += sorted[i];
    }
    double avg_worst_01pct_ms = sum_worst_01pct / static_cast<double>(count_01pct);
    s.low_01pct_fps = (avg_worst_01pct_ms > 0.0) ? (1000.0 / avg_worst_01pct_ms) : 0.0;

    return s;
}

} // namespace APDS
