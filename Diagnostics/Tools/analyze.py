#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Advanced Performance & Diagnostics System (APDS) - CLI Analyzer
Interactive inspection and diagnostic tool for developers and Antigravity.
"""

import argparse
import csv
import json
import os
import sys
from pathlib import Path

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def find_sessions(base_dir: Path):
    sessions_dir = base_dir / "Diagnostics" / "Sessions"
    if not sessions_dir.exists():
        return []
    sessions = [p for p in sessions_dir.iterdir() if p.is_dir() and not p.name.startswith(".")]
    sessions.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return sessions


def get_target_session(base_dir: Path, session_arg: str, latest: bool):
    sessions = find_sessions(base_dir)
    if not sessions:
        print("No diagnostic sessions found in Diagnostics/Sessions/.", file=sys.stderr)
        return None

    if latest or not session_arg:
        return sessions[0]

    # Try exact match or substring match
    for s in sessions:
        if s.name == session_arg or session_arg in s.name:
            return s
    
    p = Path(session_arg)
    if p.exists() and p.is_dir():
        return p

    print(f"Session '{session_arg}' not found.", file=sys.stderr)
    return None


def print_session_header(session_path: Path):
    print("=" * 80)
    print(f" APDS SESSION: {session_path.name}")
    print("=" * 80)

    summary_file = session_path / "summary.json"
    manifest_file = session_path / "session_manifest.json"
    hw_file = session_path / "hardware_profile.json"

    if summary_file.exists():
        with open(summary_file, "r", encoding="utf-8") as f:
            sm = json.load(f)
            hw = sm.get("hardware_summary", {})
            print(f"Verdict:     {sm.get('verdict', 'UNKNOWN')}")
            print(f"Hardware:    {hw.get('cpu')} | {hw.get('gpu')} ({hw.get('dedicated_vram_mb')} MB VRAM)")
            print(f"OS/RAM:      {hw.get('os')} | {hw.get('ram_gb')} GB RAM")
    elif manifest_file.exists():
        with open(manifest_file, "r", encoding="utf-8") as f:
            mf = json.load(f)
            print(f"Mode:        {mf.get('telemetry_mode')} | Duration: {mf.get('duration_seconds')}s | Total Frames: {mf.get('total_frames')}")
            print(f"FPS Avg:     {mf.get('avg_fps')} | 1% Low: {mf.get('low_1pct_fps')} | Stutters: {mf.get('total_stutters')}")


def display_frames(session_path: Path):
    print("\n--- [ FRAME PERFORMANCE & PACING ] ---")
    manifest_file = session_path / "session_manifest.json"
    frames_file = session_path / "frame_times.csv"

    if manifest_file.exists():
        with open(manifest_file, "r", encoding="utf-8") as f:
            mf = json.load(f)
            print(f"Total Frames:       {mf.get('total_frames')}")
            print(f"Duration:           {mf.get('duration_seconds')} s")
            print(f"Average FPS:        {mf.get('avg_fps'):.2f} FPS")
            print(f"Min / Max FPS:      {mf.get('min_fps'):.2f} / {mf.get('max_fps'):.2f} FPS")
            print(f"1% Low FPS:         {mf.get('low_1pct_fps'):.2f} FPS (Target: >= 50.0)")
            print(f"0.1% Low FPS:       {mf.get('low_01pct_fps'):.2f} FPS (Target: >= 40.0)")
            print(f"P50 Frame Time:     {mf.get('p50_frame_time_ms'):.2f} ms (Target: 16.66 ms)")
            print(f"P95 Frame Time:     {mf.get('p95_frame_time_ms'):.2f} ms")
            print(f"P99 Frame Time:     {mf.get('p99_frame_time_ms'):.2f} ms")
            print(f"Avg Jitter:         {mf.get('avg_jitter_ms'):.3f} ms")
            print(f"Stutters Detected:  {mf.get('total_stutters')}")
            return

    if frames_file.exists():
        with open(frames_file, "r", encoding="utf-8") as f:
            reader = list(csv.DictReader(f))
            if reader:
                times = [float(r["frame_time_ms"]) for r in reader]
                jitters = [float(r["jitter_ms"]) for r in reader]
                stutters = sum(1 for r in reader if int(r.get("is_stutter", 0)) == 1)
                total_frames = len(times)
                duration_s = sum(times) / 1000.0 if sum(times) > 0 else 0.001
                avg_fps = total_frames / duration_s
                sorted_times = sorted(times)
                p50 = sorted_times[int(len(sorted_times) * 0.50)]
                p95 = sorted_times[int(len(sorted_times) * 0.95)]
                p99 = sorted_times[int(len(sorted_times) * 0.99)]
                min_fps = 1000.0 / sorted_times[-1] if sorted_times[-1] > 0 else 0.0
                max_fps = 1000.0 / sorted_times[0] if sorted_times[0] > 0 else 0.0
                cnt_1pct = max(1, len(sorted_times) // 100)
                low_1pct = 1000.0 / (sum(sorted_times[-cnt_1pct:]) / cnt_1pct)
                cnt_01pct = max(1, len(sorted_times) // 1000)
                low_01pct = 1000.0 / (sum(sorted_times[-cnt_01pct:]) / cnt_01pct)
                avg_jitter = sum(jitters) / total_frames

                print(f"Total Frames:       {total_frames}")
                print(f"Duration:           {duration_s:.2f} s")
                print(f"Average FPS:        {avg_fps:.2f} FPS")
                print(f"Min / Max FPS:      {min_fps:.2f} / {max_fps:.2f} FPS")
                print(f"1% Low FPS:         {low_1pct:.2f} FPS (Target: >= 50.0)")
                print(f"0.1% Low FPS:       {low_01pct:.2f} FPS (Target: >= 40.0)")
                print(f"P50 Frame Time:     {p50:.2f} ms (Target: 16.66 ms)")
                print(f"P95 Frame Time:     {p95:.2f} ms")
                print(f"P99 Frame Time:     {p99:.2f} ms")
                print(f"Avg Jitter:         {avg_jitter:.3f} ms")
                print(f"Stutters Detected:  {stutters}")


def display_cpu(session_path: Path):
    print("\n--- [ CPU TELEMETRY & TOPOLOGY ] ---")
    hw_file = session_path / "hardware_profile.json"
    if hw_file.exists():
        with open(hw_file, "r", encoding="utf-8") as f:
            hw = json.load(f)
            cpu = hw.get("cpu", {})
            print(f"Processor:          {cpu.get('brand')}")
            print(f"Topology:           {cpu.get('physical_cores')} Physical Cores, {cpu.get('logical_processors')} Threads")
            print(f"Caches:             L1: {cpu.get('l1_cache_bytes')//1024} KB | L2: {cpu.get('l2_cache_bytes')//1024} KB | L3: {cpu.get('l3_cache_bytes')//(1024*1024)} MB")
            print(f"SIMD Features:      AVX2: {cpu.get('has_avx2')} | AVX-512: {cpu.get('has_avx512')}")

    cpu_csv = session_path / "cpu_samples.csv"
    if cpu_csv.exists():
        with open(cpu_csv, "r", encoding="utf-8") as f:
            reader = list(csv.DictReader(f))
            if reader:
                avg_tot = sum(float(r["total_cpu_percent"]) for r in reader) / len(reader)
                avg_proc = sum(float(r["process_cpu_percent"]) for r in reader) / len(reader)
                temps = [float(r["cpu_temp_c"]) for r in reader if float(r.get("cpu_temp_c", -1)) > 0]
                avg_temp = (sum(temps) / len(temps)) if temps else None
                print(f"Avg Total CPU:      {avg_tot:.1f}%")
                print(f"Avg Process CPU:    {avg_proc:.1f}%")
                if avg_temp:
                    print(f"Avg CPU Temp:       {avg_temp:.1f} °C")


def display_gpu(session_path: Path):
    print("\n--- [ GPU TELEMETRY & VRAM ] ---")
    hw_file = session_path / "hardware_profile.json"
    if hw_file.exists():
        with open(hw_file, "r", encoding="utf-8") as f:
            hw = json.load(f)
            gpu = hw.get("gpu", {})
            print(f"Adapter:            {gpu.get('adapter_name')}")
            print(f"Vendor / Device ID: 0x{gpu.get('vendor_id', 0):04x} / 0x{gpu.get('device_id', 0):04x}")
            print(f"Dedicated VRAM:     {gpu.get('dedicated_vram_bytes', 0) // (1024*1024)} MB")
            print(f"Shared VRAM:        {gpu.get('shared_vram_bytes', 0) // (1024*1024)} MB")

    gpu_csv = session_path / "gpu_samples.csv"
    if gpu_csv.exists():
        with open(gpu_csv, "r", encoding="utf-8") as f:
            reader = list(csv.DictReader(f))
            if reader:
                eng_vals = [float(r["gpu_engine_3d_percent"]) for r in reader if float(r["gpu_engine_3d_percent"]) >= 0]
                vram_vals = [int(r["vram_local_used_bytes"]) // (1024*1024) for r in reader]
                budget_vals = [int(r["vram_local_budget_bytes"]) // (1024*1024) for r in reader]
                temps = [float(r["gpu_temp_c"]) for r in reader if float(r["gpu_temp_c"]) > 0]

                if eng_vals:
                    print(f"Avg GPU 3D Load:    {sum(eng_vals)/len(eng_vals):.1f}% (Peak: {max(eng_vals):.1f}%)")
                if vram_vals:
                    print(f"VRAM Used (Local):  Avg {sum(vram_vals)/len(vram_vals):.0f} MB | Peak {max(vram_vals)} MB")
                if budget_vals and max(budget_vals) > 0:
                    print(f"VRAM OS Budget:     {max(budget_vals)} MB")
                if temps:
                    print(f"Avg GPU Temp:       {sum(temps)/len(temps):.1f} °C (Peak: {max(temps):.1f} °C)")


def display_stutters(session_path: Path):
    print("\n--- [ STUTTER & ANOMALY INCIDENTS ] ---")
    stutters_file = session_path / "stutters.jsonl"
    if not stutters_file.exists():
        print("No stutters log found.")
        return

    stutters = []
    with open(stutters_file, "r", encoding="utf-8") as f:
        for line in f:
            if line.strip():
                stutters.append(json.loads(line.strip()))

    if not stutters:
        print("✅ No stutters recorded during this session (100% smooth pacing).")
        return

    print(f"Found {len(stutters)} stutter incident(s):")
    for i, s in enumerate(stutters[:20], 1):
        print(f" [{i:02d}] Frame #{s.get('frame')}: {s.get('frame_time_ms'):.2f} ms (Baseline: {s.get('baseline_ms'):.2f} ms) -> Cause: {s.get('primary_suspicion')}")


def display_crashes(session_path: Path):
    print("\n--- [ CRASH & HANG DIAGNOSTICS ] ---")
    crash_file = session_path / "crash_manifest.json"
    hang_file = session_path / "hang_manifest.json"

    found = False
    if crash_file.exists():
        found = True
        with open(crash_file, "r", encoding="utf-8") as f:
            c = json.load(f)
            print("🚨 CRASH DETECTED:")
            print(f"  Timestamp:        {c.get('timestamp_iso')}")
            print(f"  Exception:        {c.get('exception_code')} ({c.get('exception_name')})")
            print(f"  Address:          {c.get('faulting_address')}")
            print(f"  Minidump:         {session_path / c.get('minidump_file', 'minidump.dmp')}")

    if hang_file.exists():
        found = True
        with open(hang_file, "r", encoding="utf-8") as f:
            h = json.load(f)
            print("⚠️ HANG / STALL DETECTED:")
            print(f"  Timestamp:        {h.get('timestamp_iso')}")
            print(f"  Stall Duration:   {h.get('stall_duration_seconds')} seconds")
            print(f"  Message:          {h.get('message')}")

    if not found:
        print("✅ Clean session. No unhandled crashes or render thread hangs occurred.")


def compare_sessions(session_a: Path, session_b: Path):
    print("=" * 80)
    print(f" APDS COMPARATIVE ANALYSIS")
    print(f" Baseline:  {session_a.name}")
    print(f" Candidate: {session_b.name}")
    print("=" * 80)

    def load_summary(path: Path):
        sm = path / "summary.json"
        if sm.exists():
            with open(sm, "r", encoding="utf-8") as f:
                return json.load(f).get("performance", {})
        mf = path / "session_manifest.json"
        if mf.exists():
            with open(mf, "r", encoding="utf-8") as f:
                return json.load(f)
        return {}

    a = load_summary(session_a)
    b = load_summary(session_b)

    metrics = [
        ("FPS Promedio", "avg_fps", "FPS", True),
        ("1% Low FPS", "low_1pct_fps", "FPS", True),
        ("0.1% Low FPS", "low_01pct_fps", "FPS", True),
        ("P50 Frametime", "p50_ms" if "p50_ms" in a else "p50_frame_time_ms", "ms", False),
        ("P99 Frametime", "p99_ms" if "p99_ms" in a else "p99_frame_time_ms", "ms", False),
        ("Avg Jitter", "avg_jitter_ms", "ms", False),
        ("Stutters", "stutters_count" if "stutters_count" in a else "total_stutters", "", False),
    ]

    print(f"{'Métrica':<20} | {'Baseline':<12} | {'Candidate':<12} | {'Delta':<12} | {'Evaluación'}")
    print("-" * 75)

    for name, key, unit, higher_is_better in metrics:
        val_a = a.get(key, 0.0)
        val_b = b.get(key, 0.0)
        delta = val_b - val_a
        delta_str = f"{delta:+.2f} {unit}".strip()

        if higher_is_better:
            status = "✅ Mejora" if delta > 1.0 else ("⚠️ Regresión" if delta < -1.5 else "➖ Equivalente")
        else:
            status = "✅ Mejora" if delta < -0.5 else ("⚠️ Regresión" if delta > 0.8 else "➖ Equivalente")

        print(f"{name:<20} | {val_a:<10.2f}{unit} | {val_b:<10.2f}{unit} | {delta_str:<12} | {status}")


def main():
    parser = argparse.ArgumentParser(description="APDS CLI Diagnostics & Performance Analyzer")
    parser.add_argument("--latest", action="store_true", help="Analyze the latest diagnostic session")
    parser.add_argument("--session", help="Session folder name or path")
    parser.add_argument("--list", action="store_true", help="List all available sessions")
    parser.add_argument("--frames", action="store_true", help="Show frame pacing and FPS stats")
    parser.add_argument("--cpu", action="store_true", help="Show CPU load and topology")
    parser.add_argument("--gpu", action="store_true", help="Show GPU 3D engine, VRAM, and clocks")
    parser.add_argument("--stutters", action="store_true", help="Show stutter incidents")
    parser.add_argument("--crashes", action="store_true", help="Check for crash minidumps or hangs")
    parser.add_argument("--all", action="store_true", help="Show all diagnostic sections")
    parser.add_argument("--compare", nargs=2, metavar=("BASELINE", "CANDIDATE"), help="Compare two sessions side-by-side")
    parser.add_argument("--export-report", action="store_true", help="Regenerate summary.json, MD, and HTML reports")

    args = parser.parse_args()
    base_dir = Path.cwd()

    if args.list:
        sessions = find_sessions(base_dir)
        print(f"Found {len(sessions)} session(s):")
        for s in sessions:
            print(f" - {s.name}")
        return

    if args.compare:
        s_a = get_target_session(base_dir, args.compare[0], False)
        s_b = get_target_session(base_dir, args.compare[1], False)
        if s_a and s_b:
            compare_sessions(s_a, s_b)
        return

    target = get_target_session(base_dir, args.session, args.latest)
    if not target:
        return

    if args.export_report:
        import report_generator
        report_generator.generate_reports(target)

    print_session_header(target)

    show_all = args.all or (not args.frames and not args.cpu and not args.gpu and not args.stutters and not args.crashes)

    if show_all or args.frames:
        display_frames(target)
    if show_all or args.cpu:
        display_cpu(target)
    if show_all or args.gpu:
        display_gpu(target)
    if show_all or args.stutters:
        display_stutters(target)
    if show_all or args.crashes:
        display_crashes(target)


if __name__ == "__main__":
    main()
