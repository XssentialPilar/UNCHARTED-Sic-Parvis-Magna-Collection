"""Summarize recorded memory and frame statistics without treating device loss as OOM.

Usage: python tools/analyze_performance_log.py "path/to/boot.log" [...] --json report.json
FPS values are medians of log samples, not per-frame percentiles or 1% lows.
"""
import argparse
import json
from pathlib import Path
import re
import statistics


MEMORY = re.compile(
    r'Memory: RAM (\d+) MiB private, (\d+) MiB working set; '
    r'video memory (\d+) of (\d+) MiB \(driver\); '
    r'buffer arenas (\d+) MiB \((\d+) in use, (\d+) given back so far\), '
    r'images (\d+) MiB \((\d+)\)')
MEMORY_FIELDS = ('ram_private_mib', 'ram_working_set_mib', 'vram_used_mib', 'vram_budget_mib',
                 'arena_allocated_mib', 'arena_in_use_mib', 'arena_recycled_mib',
                 'images_mib', 'image_count')
FPS = re.compile(r'Frame stats: ([\d.]+) FPS')
STALL = re.compile(r'Stall: ([\d.]+) ms frame')
PACING = re.compile(r'Frame pacing: median ([\d.]+) ms, stddev ([\d.]+) ms, '
                    r'p99 ([\d.]+) ms, (\d+) frames over 1.5x median')


def analyze(lines):
    memory, fps, stalls, fatal, pacing = [], [], [], [], []
    pressure_reports, recycle_reports, trim_reports = 0, 0, 0
    for number, line in enumerate(lines, 1):
        if match := MEMORY.search(line):
            sample = dict(zip(MEMORY_FIELDS, map(int, match.groups())))
            sample['line'] = number
            memory.append(sample)
        if match := FPS.search(line):
            fps.append(float(match[1]))
        if match := STALL.search(line):
            stalls.append(float(match[1]))
        if match := PACING.search(line):
            pacing.append(tuple(map(float, match.groups())))
        pressure_reports += 'Texture cache: memory pressure' in line
        recycle_reports += 'GPU: buffer arenas gave back' in line
        trim_reports += 'GPU: buffer arenas returning' in line
        if any(text in line for text in ('STOP:', 'ErrorDeviceLost', 'GPU fault address:')):
            fatal.append({'line': number, 'text': line.strip()})
    result = {
        'memory_samples': len(memory),
        'fps_samples': len(fps),
        'median_logged_fps': statistics.median(fps) if fps else None,
        'minimum_logged_fps': min(fps) if fps else None,
        'reported_stalls': len(stalls),
        'max_reported_stall_ms': max(stalls) if stalls else None,
        'texture_pressure_reports': pressure_reports,
        'arena_recycle_reports': recycle_reports,
        'arena_physical_trim_reports': trim_reports,
        'fatal_diagnostics': fatal,
        'pacing_windows': len(pacing),
        'median_window_frame_ms': statistics.median(p[0] for p in pacing) if pacing else None,
        'worst_window_p99_ms': max(p[2] for p in pacing) if pacing else None,
        'worst_window_stddev_ms': max(p[1] for p in pacing) if pacing else None,
    }
    if memory:
        result.update(
            first_memory=memory[0], last_memory=memory[-1],
            peak_memory={key: max(sample[key] for sample in memory) for key in MEMORY_FIELDS},
            peak_vram_sample=max(memory, key=lambda sample: sample['vram_used_mib']),
            vram_samples_above_budget=sum(sample['vram_used_mib'] > sample['vram_budget_mib']
                                          for sample in memory),
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    parser.add_argument('--json', type=Path, dest='output')
    args = parser.parse_args()
    results = []
    for path in args.logs:
        with path.open(encoding='utf-8', errors='replace') as stream:
            result = {'file': str(path.resolve()), **analyze(stream)}
        results.append(result)
        peaks = result.get('peak_memory', {})
        print(f'{path.name}: {result["memory_samples"]} memory samples; '
              f'peak VRAM {peaks.get("vram_used_mib", "n/a")} MiB; '
              f'arenas {peaks.get("arena_allocated_mib", "n/a")} MiB; '
              f'median logged FPS {result["median_logged_fps"]}; '
              f'worst window p99 {result["worst_window_p99_ms"]} ms; '
              f'largest reported stall {result["max_reported_stall_ms"]} ms')
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(results, indent=2, ensure_ascii=False) + '\n',
                               encoding='utf-8')


if __name__ == '__main__':
    main()
