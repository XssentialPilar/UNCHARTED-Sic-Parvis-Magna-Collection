# Advanced Performance & Diagnostics System (APDS) - Architecture & Telemetry Guide

## Overview

The **Advanced Performance & Diagnostics System (APDS)** is an integrated, low-overhead observability and telemetry engine built directly into *Uncharted: Sic Parvis Magna Collection*. 

It provides real-time profiling of CPU topology, per-core utilization, GPU 3D engine loads, DXGI/NVML telemetry, microsecond-accurate frame pacing (instant FPS, rolling EMA, 1% Low, 0.1% Low, P50/P95/P99 percentiles, jitter), asynchronous crash minidump capture, stall/hang detection, and automated reporting.

---

## 1. Directory Structure & Architecture

```text
Diagnostics/
├── Baselines/                    # Reference golden sessions for regression tracking
├── Comparisons/                  # Generated regression and delta comparison reports
├── Config/
│   └── telemetry.json            # Runtime configuration profiles (LIGHT, STANDARD, DEEP, FORENSIC)
├── Documentation/
│   └── TELEMETRY_GUIDE.md        # Technical specification and agent analysis guide
├── Sessions/
│   └── YYYY-MM-DD_HH-MM-SS_<id>/ # Dedicated session storage
│       ├── session_manifest.json # Consolidated session metadata and performance metrics
│       ├── hardware_profile.json # Detected CPU, GPU, VRAM, and OS topology
│       ├── frame_times.csv       # Microsecond-accurate frame times, CPU/GPU latency, jitter, stutter flag
│       ├── cpu_samples.csv       # Overall, process, per-core utilization, and thermal readings
│       ├── gpu_samples.csv       # GPU 3D utilization, temperatures, clocks, VRAM usage/budget
│       ├── memory_samples.csv    # Working set, private commit, available system RAM
│       ├── resources.jsonl       # Texture and buffer allocations, uploads, and LRU evictions
│       ├── stutters.jsonl        # Frame drop incidents with concurrent diagnostic context
│       ├── application_events.jsonl # Structured events (severity, category, thread ID, JSON payload)
│       ├── summary.json          # Agent-ready consolidated summary and bottleneck verdicts
│       ├── performance_report.md # Markdown report formatted for Git and GitHub
│       ├── performance_report.html # Standalone interactive dashboard (offline SVG graphs)
│       └── minidump.dmp          # (Only on crash) Full Windows minidump from SEH handler
└── Tools/
    ├── analyze.py                # Command-line diagnostic and comparison interface
    └── report_generator.py       # Automated Markdown & HTML report builder
```

---

## 2. Telemetry Profiles (`telemetry.json`)

| Profile | Frame Timing | Hardware Polling | Resource Tracking | Overhead | Recommended Use |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`DISABLED`** | Off | Off | Off | 0.0% | Benchmarking pure execution |
| **`LIGHT`** | 100% microsecond QPC | 1000 ms (1 Hz) | Warnings / Errors only | < 0.2% | Daily gameplay & background logging |
| **`STANDARD`** | 100% microsecond QPC | 500 ms (2 Hz) | Uploads & Evictions | < 0.5% | Default development profile |
| **`DEEP`** | 100% microsecond QPC | 100 ms (10 Hz) | All resource events | ~ 1.0% | Investigating stuttering or frame pacing anomalies |
| **`FORENSIC`** | Microsecond QPC + Sync | 50 ms (20 Hz) | Tracing + All allocations | ~ 2.5% | Debugging rare deadlocks or memory corruption |

Profiles can be configured via `Diagnostics/Config/telemetry.json` or overridden via environment variables:
- `GOW3_TELEMETRY_ENABLED=1` / `0`
- `GOW3_TELEMETRY_PROFILE=STANDARD` / `LIGHT` / `DEEP` / `FORENSIC` / `DISABLED`

---

## 3. Data Schema & File Specifications

### `session_manifest.json`
- `session_id`: Unique folder identifier.
- `telemetry_mode`: Active profile (`LIGHT`, `STANDARD`, etc.).
- `duration_seconds`: Total run length.
- `total_frames`: Number of rendered frames.
- `avg_fps`: Total frames divided by duration.
- `low_1pct_fps`: Average of the slowest 1% of frames, expressed in FPS.
- `low_01pct_fps`: Average of the slowest 0.1% of frames.
- `p50_frame_time_ms`: Median frame time (16.66 ms = 60 FPS target).
- `p95_frame_time_ms`: 95th percentile frame time.
- `p99_frame_time_ms`: 99th percentile frame time.
- `avg_jitter_ms`: Average delta between consecutive frames ($|t_n - t_{n-1}|$).
- `total_stutters`: Count of frames violating pacing invariants ($> 24.5\text{ ms}$).
- `clean_exit`: `true` if shutdown succeeded without unhandled exceptions.

### `frame_times.csv`
```csv
frame_index,timestamp_us,frame_time_ms,cpu_time_ms,gpu_time_ms,jitter_ms,draw_calls,compute_dispatches,is_stutter
```

---

## 4. CLI Analyzer Usage (`analyze.py`)

Run diagnostics directly using Python:

```bash
# Analyze latest recorded session:
python Diagnostics/Tools/analyze.py --latest

# Show frame pacing and 1% low metrics:
python Diagnostics/Tools/analyze.py --latest --frames

# Show GPU, clocks, and VRAM utilization:
python Diagnostics/Tools/analyze.py --latest --gpu

# Show CPU topology and per-core utilization:
python Diagnostics/Tools/analyze.py --latest --cpu

# Check for stutter incidents and root causes:
python Diagnostics/Tools/analyze.py --latest --stutters

# Check for crashes or watchdog hangs:
python Diagnostics/Tools/analyze.py --latest --crashes

# Compare two sessions side-by-side:
python Diagnostics/Tools/analyze.py --compare 2026-11-07_05-15-00_a1b2 2026-11-07_05-20-00_c3d4

# Regenerate HTML and Markdown reports:
python Diagnostics/Tools/analyze.py --latest --export-report
```

---

## 5. Guidelines for Antigravity Analysis

When evaluating a session or investigating regressions:
1. **Check Verdict in `summary.json`:**
   - `HEALTHY_OPTIMAL`: 60 FPS fixed, 1% Low >= 50 FPS, zero unhandled stutters.
   - `MINOR_STUTTERS`: Occasional vblank drops or asset load spikes.
   - `DEGRADED`: P99 > 25 ms, 1% Low < 40 FPS, continuous frame pacing instability.
   - `CRITICAL_CRASH` / `CRITICAL_HANG`: Check `crash_manifest.json` and `minidump.dmp`.
2. **Correlate Stutters with Resource Events:**
   - If `primary_suspicion` indicates `TEXTURE_UPLOAD_BURST`, check `resources.jsonl` around that timestamp. Verify whether `low_vram_override` is pacing uploads correctly.
   - If `CPU_BOUND_SPIKE`, check `cpu_samples.csv` to identify if a background thread saturated a physical core.
