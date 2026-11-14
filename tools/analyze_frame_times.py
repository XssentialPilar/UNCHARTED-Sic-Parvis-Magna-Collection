"""Analyze actual host presentation timestamps from an APDS frame_times.csv.

The legacy frame_time_ms column substitutes 16.666 ms for intervals over two seconds.
Reconstruct intervals from timestamp_us instead. These are host presentation intervals;
they do not measure GPU execution time or scanout time. Missing samples are reported.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def analyze(rows, start_frame=0, end_frame=None):
    intervals, jitters = [], []
    previous = None
    previous_interval = None
    dropped = invalid = 0
    for row in rows:
        frame, timestamp = int(row["frame_index"]), int(row["timestamp_us"])
        if frame < start_frame or (end_frame is not None and frame > end_frame):
            continue
        if previous is not None:
            step = frame - previous[0]
            elapsed = timestamp - previous[1]
            if step == 1 and elapsed > 0:
                milliseconds = elapsed / 1000
                intervals.append(milliseconds)
                if previous_interval is not None:
                    jitters.append(abs(milliseconds - previous_interval))
                previous_interval = milliseconds
            else:
                dropped += max(0, step - 1)
                invalid += int(step <= 0 or elapsed <= 0)
                previous_interval = None
        previous = (frame, timestamp)
    if not intervals:
        raise ValueError("No consecutive presentation timestamps in the selected range")
    ordered = sorted(intervals, reverse=True)

    def low(fraction):
        return 1000 / statistics.mean(ordered[:max(1, math.ceil(len(ordered) * fraction))])

    mean = statistics.mean(intervals)
    return {
        "measurement": "host_presentation_interval",
        "intervals": len(intervals), "missing_samples": dropped, "invalid_transitions": invalid,
        "sampled_duration_seconds": sum(intervals) / 1000,
        "average_fps": 1000 / mean, "low_1_percent_fps": low(.01),
        "low_0_1_percent_fps": low(.001), "mean_ms": mean,
        "p50_ms": percentile(intervals, .5), "p95_ms": percentile(intervals, .95),
        "p99_ms": percentile(intervals, .99), "max_ms": max(intervals),
        "standard_deviation_ms": statistics.pstdev(intervals),
        "jitter_p99_ms": percentile(jitters, .99) if jitters else None,
        "intervals_over_2_seconds": sum(value > 2000 for value in intervals),
        "cpu_execution_ms": None, "gpu_execution_ms": None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--start-frame", type=int, default=0)
    parser.add_argument("--end-frame", type=int)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with args.csv.open(encoding="utf-8", newline="") as stream:
        result = analyze(csv.DictReader(stream), args.start_frame, args.end_frame)
    result["source"] = str(args.csv.resolve())
    result["selected_start_frame"] = args.start_frame
    result["selected_end_frame"] = args.end_frame
    text = json.dumps(result, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
