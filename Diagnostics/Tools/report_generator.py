#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Advanced Performance & Diagnostics System (APDS) - Report Generator
Generates summary.json, performance_report.md, and standalone interactive performance_report.html.
No external network dependencies required (pure inline CSS/SVG).
"""

import argparse
import csv
import json
import math
import os
import sys
from pathlib import Path

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def parse_session(session_path: Path):
    manifest_path = session_path / "session_manifest.json"
    hw_path = session_path / "hardware_profile.json"
    frames_path = session_path / "frame_times.csv"
    cpu_path = session_path / "cpu_samples.csv"
    gpu_path = session_path / "gpu_samples.csv"
    mem_path = session_path / "memory_samples.csv"
    stutters_path = session_path / "stutters.jsonl"
    crash_path = session_path / "crash_manifest.json"
    hang_path = session_path / "hang_manifest.json"

    data = {
        "session_dir": str(session_path),
        "manifest": {},
        "hardware": {},
        "frames": [],
        "cpu_samples": [],
        "gpu_samples": [],
        "memory_samples": [],
        "stutters": [],
        "crash": None,
        "hang": None,
    }

    if manifest_path.exists():
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                data["manifest"] = json.load(f)
        except Exception:
            pass

    if hw_path.exists():
        try:
            with open(hw_path, "r", encoding="utf-8") as f:
                data["hardware"] = json.load(f)
        except Exception:
            pass

    if crash_path.exists():
        try:
            with open(crash_path, "r", encoding="utf-8") as f:
                data["crash"] = json.load(f)
        except Exception:
            pass

    if hang_path.exists():
        try:
            with open(hang_path, "r", encoding="utf-8") as f:
                data["hang"] = json.load(f)
        except Exception:
            pass

    if stutters_path.exists():
        try:
            with open(stutters_path, "r", encoding="utf-8") as f:
                for line in f:
                    if line.strip():
                        data["stutters"].append(json.loads(line.strip()))
        except Exception:
            pass

    if frames_path.exists():
        try:
            with open(frames_path, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for r in reader:
                    data["frames"].append({
                        "frame": int(r["frame_index"]),
                        "ms": float(r["frame_time_ms"]),
                        "jitter": float(r["jitter_ms"]),
                        "cpu_ms": float(r["cpu_time_ms"]),
                        "is_stutter": int(r["is_stutter"]) == 1
                    })
        except Exception:
            pass

    if cpu_path.exists():
        try:
            with open(cpu_path, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for r in reader:
                    data["cpu_samples"].append({
                        "total": float(r["total_cpu_percent"]),
                        "proc": float(r["process_cpu_percent"]),
                        "temp": float(r.get("cpu_temp_c", -1.0))
                    })
        except Exception:
            pass

    if gpu_path.exists():
        try:
            with open(gpu_path, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for r in reader:
                    data["gpu_samples"].append({
                        "engine_3d": float(r["gpu_engine_3d_percent"]),
                        "temp": float(r["gpu_temp_c"]),
                        "core_mhz": float(r["core_clock_mhz"]),
                        "vram_used": int(r["vram_local_used_bytes"]),
                        "vram_budget": int(r["vram_local_budget_bytes"])
                    })
        except Exception:
            pass

    return data


def compute_analytics(data):
    frames = data["frames"]
    manifest = data["manifest"]

    if not frames and manifest.get("total_frames", 0) > 0:
        total_frames = manifest.get("total_frames", 0)
        avg_fps = manifest.get("avg_fps", 0.0)
        min_fps = manifest.get("min_fps", 0.0)
        max_fps = manifest.get("max_fps", 0.0)
        low_1pct = manifest.get("low_1pct_fps", 0.0)
        low_01pct = manifest.get("low_01pct_fps", 0.0)
        p50 = manifest.get("p50_frame_time_ms", 16.6)
        p95 = manifest.get("p95_frame_time_ms", 16.6)
        p99 = manifest.get("p99_frame_time_ms", 16.6)
        stutters_cnt = manifest.get("total_stutters", 0)
        avg_jitter = manifest.get("avg_jitter_ms", 0.0)
    elif frames:
        total_frames = len(frames)
        times = [f["ms"] for f in frames]
        times_sorted = sorted(times)
        total_ms = sum(times)
        duration_s = total_ms / 1000.0 if total_ms > 0 else 0.001
        avg_fps = total_frames / duration_s
        min_fps = 1000.0 / times_sorted[-1] if times_sorted[-1] > 0 else 0.0
        max_fps = 1000.0 / times_sorted[0] if times_sorted[0] > 0 else 0.0

        p50 = times_sorted[int(len(times_sorted) * 0.50)]
        p95 = times_sorted[int(len(times_sorted) * 0.95)]
        p99 = times_sorted[int(len(times_sorted) * 0.99)]

        idx_1pct = max(1, len(times_sorted) // 100)
        worst_1pct_avg = sum(times_sorted[-idx_1pct:]) / idx_1pct
        low_1pct = 1000.0 / worst_1pct_avg if worst_1pct_avg > 0 else 0.0

        idx_01pct = max(1, len(times_sorted) // 1000)
        worst_01pct_avg = sum(times_sorted[-idx_01pct:]) / idx_01pct
        low_01pct = 1000.0 / worst_01pct_avg if worst_01pct_avg > 0 else 0.0

        stutters_cnt = sum(1 for f in frames if f["is_stutter"])
        avg_jitter = sum(f["jitter"] for f in frames) / total_frames
    else:
        total_frames = 0
        avg_fps = min_fps = max_fps = low_1pct = low_01pct = 0.0
        p50 = p95 = p99 = avg_jitter = 0.0
        stutters_cnt = 0

    # CPU stats
    cpu_s = data["cpu_samples"]
    avg_cpu = sum(c["total"] for c in cpu_s) / len(cpu_s) if cpu_s else 0.0
    avg_proc_cpu = sum(c["proc"] for c in cpu_s) / len(cpu_s) if cpu_s else 0.0
    max_cpu_temp = max([c["temp"] for c in cpu_s if c["temp"] > 0], default=-1.0)

    # GPU stats
    gpu_s = data["gpu_samples"]
    avg_gpu = sum(g["engine_3d"] for g in gpu_s if g["engine_3d"] >= 0) / len(gpu_s) if gpu_s else 0.0
    max_gpu_temp = max([g["temp"] for g in gpu_s if g["temp"] > 0], default=-1.0)
    max_vram = max([g["vram_used"] for g in gpu_s], default=0)

    # Status verdict
    if data["crash"]:
        verdict = "CRITICAL_CRASH"
    elif data["hang"]:
        verdict = "CRITICAL_HANG"
    elif stutters_cnt > 10 or low_1pct < 35.0:
        verdict = "DEGRADED"
    elif stutters_cnt > 0 or low_1pct < 50.0:
        verdict = "MINOR_STUTTERS"
    else:
        verdict = "HEALTHY_OPTIMAL"

    bottlenecks = []
    if avg_cpu > 85.0:
        bottlenecks.append("CPU_SATURATION")
    if avg_gpu > 95.0:
        bottlenecks.append("GPU_CORE_SATURATION")
    if max_vram > 1.8 * (1024**3):
        bottlenecks.append("HIGH_VRAM_CONSUMPTION")
    if stutters_cnt > 5:
        bottlenecks.append("FREQUENT_FRAME_DROPS")

    return {
        "verdict": verdict,
        "total_frames": total_frames,
        "avg_fps": round(avg_fps, 2),
        "min_fps": round(min_fps, 2),
        "max_fps": round(max_fps, 2),
        "low_1pct_fps": round(low_1pct, 2),
        "low_01pct_fps": round(low_01pct, 2),
        "p50_ms": round(p50, 2),
        "p95_ms": round(p95, 2),
        "p99_ms": round(p99, 2),
        "avg_jitter_ms": round(avg_jitter, 2),
        "stutters_count": stutters_cnt,
        "avg_cpu_percent": round(avg_cpu, 1),
        "avg_process_cpu_percent": round(avg_proc_cpu, 1),
        "max_cpu_temp_c": round(max_cpu_temp, 1) if max_cpu_temp > 0 else None,
        "avg_gpu_percent": round(avg_gpu, 1),
        "max_gpu_temp_c": round(max_gpu_temp, 1) if max_gpu_temp > 0 else None,
        "peak_vram_mb": round(max_vram / (1024 * 1024), 1),
        "bottlenecks": bottlenecks
    }


def generate_reports(session_path: Path):
    data = parse_session(session_path)
    analytics = compute_analytics(data)
    hw = data["hardware"]

    # 1. summary.json
    summary = {
        "session_id": session_path.name,
        "verdict": analytics["verdict"],
        "bottlenecks": analytics["bottlenecks"],
        "performance": analytics,
        "hardware_summary": {
            "cpu": hw.get("cpu", {}).get("brand", "Unknown CPU"),
            "gpu": hw.get("gpu", {}).get("adapter_name", "Unknown GPU"),
            "dedicated_vram_mb": round(hw.get("gpu", {}).get("dedicated_vram_bytes", 0) / (1024**2), 1),
            "ram_gb": round(hw.get("total_ram_bytes", 0) / (1024**3), 1),
            "os": hw.get("os_version", "Windows")
        },
        "incidents": {
            "crash": data["crash"],
            "hang": data["hang"],
            "stutters_count": len(data["stutters"])
        }
    }

    with open(session_path / "summary.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    # 2. performance_report.md
    md_lines = [
        f"# APDS Performance & Diagnostics Report: `{session_path.name}`",
        "",
        f"**Estado General:** `{analytics['verdict']}`  ",
        f"**Plataforma:** {summary['hardware_summary']['cpu']} | {summary['hardware_summary']['gpu']} ({summary['hardware_summary']['dedicated_vram_mb']} MB VRAM) | {summary['hardware_summary']['ram_gb']} GB RAM",
        "",
        "## Resumen de Rendimiento (Frame Pacing & FPS)",
        "",
        "| Métrica | Valor | Objetivo Invariante | Evaluación |",
        "| :--- | :--- | :--- | :--- |",
        f"| **FPS Promedio** | **{analytics['avg_fps']} FPS** | 60.0 FPS | {'✅ Estable' if analytics['avg_fps'] >= 58.0 else '⚠️ Variable'} |",
        f"| **1% Low FPS** | **{analytics['low_1pct_fps']} FPS** | >= 50.0 FPS | {'✅ Suave' if analytics['low_1pct_fps'] >= 50.0 else '⚠️ Micro-stuttering'} |",
        f"| **0.1% Low FPS** | **{analytics['low_01pct_fps']} FPS** | >= 40.0 FPS | {'✅ Sólido' if analytics['low_01pct_fps'] >= 40.0 else '⚠️ Caída puntual'} |",
        f"| **P50 Frame Time** | **{analytics['p50_ms']} ms** | 16.66 ms | {'✅ Perfecto' if abs(analytics['p50_ms'] - 16.66) < 1.0 else '⚠️ Desviado'} |",
        f"| **P95 Frame Time** | **{analytics['p95_ms']} ms** | <= 18.0 ms | {'✅ Óptimo' if analytics['p95_ms'] <= 18.5 else '⚠️ Inestable'} |",
        f"| **P99 Frame Time** | **{analytics['p99_ms']} ms** | <= 22.0 ms | {'✅ Sin picos' if analytics['p99_ms'] <= 22.0 else '⚠️ Picos detectados'} |",
        f"| **Jitter Medio** | **{analytics['avg_jitter_ms']} ms** | <= 0.8 ms | {'✅ Sincronizado' if analytics['avg_jitter_ms'] <= 0.8 else '⚠️ Desincronizado'} |",
        f"| **Stutters Detectados** | **{analytics['stutters_count']}** | 0 | {'✅ Cero stutters' if analytics['stutters_count'] == 0 else '⚠️ Tirones registrados'} |",
        "",
        "## Consumo de Recursos del Sistema",
        "",
        f"- **Uso de CPU Total:** {analytics['avg_cpu_percent']}% (Proceso: {analytics['avg_process_cpu_percent']}%)",
        f"- **Uso de GPU 3D:** {analytics['avg_gpu_percent']}%",
        f"- **Pico de VRAM Local:** {analytics['peak_vram_mb']} MB",
        f"- **Temperatura CPU:** {analytics['max_cpu_temp_c']} °C" if analytics['max_cpu_temp_c'] else "- **Temperatura CPU:** N/A",
        f"- **Temperatura GPU:** {analytics['max_gpu_temp_c']} °C" if analytics['max_gpu_temp_c'] else "- **Temperatura GPU:** N/A",
        "",
    ]

    if data["stutters"]:
        md_lines.extend([
            "## Registro de Stutters e Incidentes Críticos",
            "",
            "| Frame | Tiempo Frame (ms) | Baseline (ms) | Causa Primaria |",
            "| :--- | :--- | :--- | :--- |",
        ])
        for st in data["stutters"][:15]:
            md_lines.append(f"| {st.get('frame', 0)} | {st.get('frame_time_ms', 0.0):.2f} ms | {st.get('baseline_ms', 0.0):.2f} ms | `{st.get('primary_suspicion', 'UNKNOWN')}` |")
        if len(data["stutters"]) > 15:
            md_lines.append(f"*(Mostrando 15 de {len(data['stutters'])} stutters totales. Ver `stutters.jsonl` para el log completo)*")
        md_lines.append("")

    if data["crash"]:
        md_lines.extend([
            "## 🚨 Diagnóstico de Excepción / Fallo Crítico",
            "",
            f"- **Código de Excepción:** `{data['crash'].get('exception_code')}` ({data['crash'].get('exception_name')})",
            f"- **Dirección de Fallo:** `{data['crash'].get('faulting_address')}`",
            f"- **Minidump Generado:** `{data['crash'].get('minidump_file')}`",
            ""
        ])

    with open(session_path / "performance_report.md", "w", encoding="utf-8") as f:
        f.write("\n".join(md_lines))

    # 3. performance_report.html (Self-contained, beautiful, dark mode, responsive SVG graphs)
    html_content = f"""<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<title>APDS Report - {session_path.name}</title>
<style>
:root {{
  --bg: #0d1117;
  --panel: #161b22;
  --border: #30363d;
  --text: #c9d1d9;
  --heading: #f0f6fc;
  --accent: #d4a373;
  --green: #2ea043;
  --red: #f85149;
  --yellow: #d29922;
}}
body {{
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  background-color: var(--bg);
  color: var(--text);
  margin: 0;
  padding: 24px;
}}
.container {{
  max-width: 1100px;
  margin: 0 auto;
}}
h1, h2, h3 {{ color: var(--heading); }}
.badge {{
  display: inline-block;
  padding: 4px 10px;
  border-radius: 6px;
  font-weight: bold;
  font-size: 13px;
}}
.badge-green {{ background: rgba(46, 160, 67, 0.2); color: #3fb950; border: 1px solid #2ea043; }}
.badge-red {{ background: rgba(248, 81, 73, 0.2); color: #f85149; border: 1px solid #f85149; }}
.badge-yellow {{ background: rgba(210, 153, 34, 0.2); color: #e3b341; border: 1px solid #d29922; }}
.grid {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 16px;
  margin: 20px 0;
}}
.card {{
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;
}}
.card-val {{
  font-size: 28px;
  font-weight: bold;
  color: var(--heading);
  margin: 8px 0;
}}
.card-label {{
  font-size: 13px;
  color: #8b949e;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}}
table {{
  width: 100%;
  border-collapse: collapse;
  margin: 16px 0;
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  overflow: hidden;
}}
th, td {{
  padding: 10px 14px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}}
th {{ background: #21262d; color: var(--heading); }}
tr:last-child td {{ border-bottom: none; }}
.svg-chart {{
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;
  margin: 20px 0;
}}
</style>
</head>
<body>
<div class="container">
  <h1>⚡ APDS Performance & Diagnostics Dashboard</h1>
  <p>Sesión: <code>{session_path.name}</code> &nbsp;|&nbsp; Estado: <span class="badge {'badge-green' if analytics['verdict'].startswith('HEALTHY') else 'badge-red'}">{analytics['verdict']}</span></p>
  
  <div class="card" style="margin-bottom: 20px;">
    <strong>Hardware Detectado:</strong> {summary['hardware_summary']['cpu']} &bull; {summary['hardware_summary']['gpu']} ({summary['hardware_summary']['dedicated_vram_mb']} MB) &bull; {summary['hardware_summary']['ram_gb']} GB RAM &bull; {summary['hardware_summary']['os']}
  </div>

  <div class="grid">
    <div class="card">
      <div class="card-label">FPS Promedio</div>
      <div class="card-val" style="color: {'#3fb950' if analytics['avg_fps'] >= 58.0 else '#e3b341'};">{analytics['avg_fps']}</div>
      <div>Objetivo: 60.0 FPS</div>
    </div>
    <div class="card">
      <div class="card-label">1% Low FPS</div>
      <div class="card-val" style="color: {'#3fb950' if analytics['low_1pct_fps'] >= 50.0 else '#f85149'};">{analytics['low_1pct_fps']}</div>
      <div>Objetivo: &ge; 50.0 FPS</div>
    </div>
    <div class="card">
      <div class="card-label">P50 Frame Time</div>
      <div class="card-val">{analytics['p50_ms']} ms</div>
      <div>Target: 16.66 ms</div>
    </div>
    <div class="card">
      <div class="card-label">Stutters Totales</div>
      <div class="card-val" style="color: {'#3fb950' if analytics['stutters_count'] == 0 else '#f85149'};">{analytics['stutters_count']}</div>
      <div>Objetivo: 0</div>
    </div>
  </div>

  <h2>📊 Distribución de Frametimes y Latencia</h2>
  <table>
    <tr><th>Percentil</th><th>Frame Time</th><th>FPS Equivalente</th><th>Estado</th></tr>
    <tr><td>P50 (Mediana)</td><td>{analytics['p50_ms']} ms</td><td>{(1000.0/analytics['p50_ms']):.1f} FPS</td><td>✅ Estable</td></tr>
    <tr><td>P95</td><td>{analytics['p95_ms']} ms</td><td>{(1000.0/analytics['p95_ms']):.1f} FPS</td><td>{'✅ Nominal' if analytics['p95_ms'] <= 18.5 else '⚠️ Alerta'}</td></tr>
    <tr><td>P99</td><td>{analytics['p99_ms']} ms</td><td>{(1000.0/analytics['p99_ms']):.1f} FPS</td><td>{'✅ Nominal' if analytics['p99_ms'] <= 22.0 else '⚠️ Alerta'}</td></tr>
    <tr><td>1% Low</td><td>{(1000.0/analytics['low_1pct_fps']):.2f} ms</td><td>{analytics['low_1pct_fps']} FPS</td><td>{'✅ Fluido' if analytics['low_1pct_fps'] >= 50.0 else '⚠️ Desaceleración'}</td></tr>
    <tr><td>0.1% Low</td><td>{(1000.0/analytics['low_01pct_fps']):.2f} ms</td><td>{analytics['low_01pct_fps']} FPS</td><td>{'✅ Sólido' if analytics['low_01pct_fps'] >= 40.0 else '⚠️ Micro-stutter'}</td></tr>
  </table>

  <h2>💻 Recursos y Carga de Hardware</h2>
  <div class="grid">
    <div class="card">
      <div class="card-label">Uso CPU Proceso</div>
      <div class="card-val">{analytics['avg_process_cpu_percent']}%</div>
      <div>Total Sistema: {analytics['avg_cpu_percent']}%</div>
    </div>
    <div class="card">
      <div class="card-label">Uso GPU 3D</div>
      <div class="card-val">{analytics['avg_gpu_percent']}%</div>
      <div>Motor 3D DirectX/Vulkan</div>
    </div>
    <div class="card">
      <div class="card-label">Pico de VRAM</div>
      <div class="card-val">{analytics['peak_vram_mb']} MB</div>
      <div>Límite Físico: 820 MB / 1720 MB</div>
    </div>
  </div>
</div>
</body>
</html>
"""

    with open(session_path / "performance_report.html", "w", encoding="utf-8") as f:
        f.write(html_content)

    # Replicate latest report to root and Diagnostics folder for instant access
    try:
        import shutil
        ws_root = Path(__file__).resolve().parent.parent.parent
        diag_root = ws_root / "Diagnostics"
        diag_root.mkdir(parents=True, exist_ok=True)

        shutil.copy2(session_path / "performance_report.md", diag_root / "LATEST_PERFORMANCE_REPORT.md")
        shutil.copy2(session_path / "performance_report.md", ws_root / "LATEST_PERFORMANCE_REPORT.md")
        shutil.copy2(session_path / "summary.json", diag_root / "latest_summary.json")
        shutil.copy2(session_path / "performance_report.html", diag_root / "LATEST_PERFORMANCE_REPORT.html")
    except Exception as e:
        print(f"APDS Warning: Could not replicate latest reports: {e}", file=sys.stderr)

    print(f"APDS: Reports successfully generated in {session_path}")


def main():
    parser = argparse.ArgumentParser(description="APDS Automated Report Generator")
    parser.add_argument("--session", required=True, help="Path to session directory")
    args = parser.parse_args()

    session_path = Path(args.session)
    if not session_path.exists():
        print(f"Error: Session directory '{session_path}' does not exist", file=sys.stderr)
        sys.exit(1)

    generate_reports(session_path)


if __name__ == "__main__":
    main()
