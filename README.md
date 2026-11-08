# UNCHARTED: Sic Parvis Magna Collection

> **Sic Parvis Magna** — *Greatness from small beginnings.*

![Development](https://img.shields.io/badge/Status-Active_Development-C88F4D?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows-1F6FEB?style=flat-square)
![Language](https://img.shields.io/badge/Language-C%2B%2B20%20%2F%20C%2B%2B23-00599C?style=flat-square)
![Focus](https://img.shields.io/badge/Focus-Performance_%26_Compatibility-3A7D75?style=flat-square)

**A community-driven PC runtime and compatibility project built around a simple idea: great games deserve to remain accessible, adaptable, and enjoyable on more than just the latest hardware.**

UNCHARTED: Sic Parvis Magna Collection explores how careful engineering can improve the way classic UNCHARTED experiences are presented and managed on PC. Its focus is on display adaptation, input consistency, memory efficiency, and predictable performance—without losing sight of visual quality.

This is an **independent, unofficial engineering project**, not an official PC release or a distribution of the original games.

---

## The Vision

Modern PC gaming should offer more freedom than a fixed resolution, a fixed graphics preset, or a single class of hardware.

The project is being developed around three principles:

1. **Accessibility across hardware generations.** Explore practical ways to make demanding game workloads more manageable on older and memory-constrained PCs.
2. **Faithful presentation.** Adapt image framing, viewport geometry, HUD elements, and input coordinates while respecting the original experience.
3. **Consistency over headline numbers.** Prioritize smooth frame delivery, stable memory behavior, and responsive controls—not just the highest momentary FPS reading.

The long-term ambition is to combine console-inspired visual presentation with the flexibility expected from a native PC experience.

## What the Project Focuses On

| Area | Intended player benefit |
| :--- | :--- |
| **Display adaptation** | More consistent presentation across supported resolutions and aspect ratios, with correctly scaled UI and input coordinates. |
| **Hardware-aware graphics settings** | Quality options designed around the resources actually available on the system. |
| **VRAM budget management** | Fewer avoidable memory spikes through telemetry, texture residency policies, and resource reuse. |
| **Frame pacing** | Better frame-time consistency and less visible stutter during camera movement and gameplay transitions. |
| **Texture efficiency** | Mip selection and appropriate block-compressed formats to reduce memory pressure where supported. |
| **Keyboard and mouse adaptation** | Clearer PC-oriented controls and correctly transformed input across viewport changes. |
| **Diagnostics** | Performance and memory measurements that make problems easier to reproduce and investigate. |

These are the project's **design priorities**. Individual features may be experimental, incomplete, or dependent on the selected graphics backend.

## Why It Matters

Many performance problems are not caused by visual complexity alone. Poor resource residency decisions, irregular frame pacing, repeated allocations, and incorrect display scaling can waste hardware capability and undermine an otherwise playable experience.

This project approaches those problems as interconnected systems:

- **Use memory deliberately.** Keep essential resources available, reduce unnecessary high-resolution residency, and avoid preventable allocation churn.
- **Scale the expensive work.** Make detail, shadow quality, and render distance respond to the selected performance profile rather than assuming unlimited GPU resources.
- **Keep the image coherent.** Treat resolution, aspect ratio, UI layout, and pointer mapping as one display system.
- **Measure the experience.** Track frame times, memory use, and compatibility issues so improvements can be assessed with evidence.

The goal is not simply to make a game *launch* on weaker hardware. It is to investigate what it takes to make the experience **usable, stable, and visually convincing** within realistic constraints.

## Built for Different Kinds of Players

**For players:** A focus on configurable graphics, reliable controls, and smoother presentation on a wider range of systems.

**For owners of older PCs:** A hardware-conscious design philosophy centered on 1 GB and 2 GB VRAM reference profiles instead of assuming a modern high-end GPU.

**For developers and technical contributors:** A modular C++ codebase, measurable budgets, explicit invariants, and opportunities to validate compatibility and optimization work.

## Performance Ambitions — 2015 Hardware Baseline

The following profiles are **engineering targets, not certified minimum requirements or guaranteed results**.

| Profile | Legacy 1 GB VRAM target | 2 GB VRAM target |
| :--- | :--- | :--- |
| **Rendering goal** | 720p, low–medium quality | 1080p, PS4-inspired quality target |
| **Performance objective** | Playable 30 FPS baseline; explore 60 FPS where feasible | Stable 60 FPS where the game, driver, and hardware allow |
| **Engine-managed VRAM budget** | **820 MiB** (`859,832,320` bytes) | **1,720 MiB** (`1,803,550,720` bytes) |
| **System memory reference** | 4 GB | 6–8 GB |
| **Reference GPU classes** | GTX 460 / GTX 550 Ti / HD 5770 / HD 7750 | GTX 750 Ti / GTX 660 / HD 7850 / R9 270 |
| **Reference CPU classes** | Core 2 Quad Q8400 / Phenom II X4 945 | Core i5-2400 / FX-6300 |

**Important:** Reference components identify the historical hardware class the project aims to investigate. Their inclusion does **not** mean that each GPU, CPU, driver, or operating system has passed a compatibility or performance test. The VRAM figures describe *project-managed targets*, not an absolute limit on all allocations made by the operating system, graphics driver, or game. Zero paging and locked frame rates cannot be promised solely by selecting a memory cap.

### Graphics API and Operating System Compatibility

- **Primary graphics direction:** Direct3D 11 and feature-level capability detection, with DXGI interfaces selected according to what the operating system actually provides.
- **Vulkan:** A separate, capability-dependent path. Vulkan 1.3 must **not** be assumed available on every legacy GPU listed above.
- **Windows:** Modern Windows configurations are the practical development environment. Windows 7 SP1 and Windows 8.1 remain historical compatibility goals, subject to the actual Direct3D/DXGI feature set and successful testing.

No platform or GPU should be considered officially supported until it has been verified with repeatable tests.

## Project Status

**Active development / experimental.** The framework is evolving, and behavior may differ between titles, graphics APIs, drivers, and hardware configurations.

Current priorities include:

- Improving texture and transient-resource memory management.
- Reducing stutter through controlled streaming, workload scheduling, and frame pacing.
- Making graphics controls correspond to real runtime behavior.
- Refining keyboard/mouse prompts and display adaptation.
- Establishing reproducible tests on low-VRAM and older-generation hardware.

**Benchmarks and compatibility claims will be meaningful only when accompanied by their test conditions.** Until then, specifications on this page describe targets and architecture, not validated shipping guarantees.

---

## Technical Overview

The runtime is designed around **C++20/C++23**, explicit error handling, data-oriented structures, and bounded work on performance-sensitive paths.

### Core Architecture

| Component | Responsibility |
| :--- | :--- |
| [`LinearArenaAllocator`](Project/include/unc/core/arena_allocator.hpp) | Per-frame transient allocations, with constant-time arena reset. |
| [`FramePacer`](Project/include/unc/core/frame_pacer.hpp) | Frame timing and pacing policy. |
| [`SpscQueue`](Project/include/unc/core/spsc_queue.hpp) | Asynchronous producer/consumer handoff for streaming work. |
| [`VramBudgetManager`](Project/include/unc/graphics/vram_budget_mgr.hpp) | Memory telemetry and pressure-aware residency policy. |
| [`DynamicRingBuffer`](Project/include/unc/graphics/dynamic_ring_buffer.hpp) | Frequently updated GPU-side data. |
| [`TransientRenderTargetPool`](Project/include/unc/graphics/transient_rt_pool.hpp) | Reuse of compatible temporary render targets. |
| [`InputMapper`](Project/include/unc/input/input_mapper.hpp) | Independent X/Y coordinate normalization for adapted viewports. |

The intended engineering conventions include avoiding general-purpose dynamic allocation in the per-frame hot path, using `alignas(64)` where it demonstrably prevents false sharing, and validating relevant Windows/Direct3D results through the project's error-handling helpers. Exact implementation and platform support must be confirmed against the repository's actual headers and tests.

### VRAM Pressure Policy

Where supported by the active backend, the intended texture residency policy responds to the ratio between **measured current usage** and **available budget**:

| Pressure | Usage / Budget | Intended action |
| :--- | :--- | :--- |
| Normal | Below 80% | Allow appropriate higher-detail texture residency. |
| Elevated | 80%–88% | Restrict nonessential high-resolution mip promotions. |
| Critical | 88% or above | Prioritize safe eviction or downgrade candidates and reclaim reusable transient resources. |

The design also explores BC1–BC7 texture formats, BC5 normal-map reconstruction, pinned low-resolution mip tails, and reuse of transient render-target allocations. A high-detail mip downgrade can reduce a texture's *theoretical* footprint by roughly 75% if the top mip level accounts for that proportion, but actual residency savings depend on resource creation and backend support.

Temporary render targets are intended to share compatible allocations when their lifetimes do not overlap—for example, shadow, lighting, and post-processing phases. Reuse must respect GPU synchronization and resource format requirements.

### Frame-Time and Presentation Targets

A 60 FPS output cadence corresponds to a **16.667 ms** frame interval. The framework uses this as a reference when profiling input/UI work, CPU submission, GPU execution, and presentation overhead.

The project's more aggressive frame-to-frame jitter objective is **below 0.45 ms** under controlled conditions. This is a measurement target—not a universal invariant that can be guaranteed across operating systems, scenes, or hardware. CPU and GPU phases may overlap, so subsystem budgets must be evaluated using captured traces rather than simply added together.

Display adaptation treats viewport dimensions and input coordinates separately:

```text
gameX = (clientX - viewportX) * internalWidth  / viewportWidth
gameY = (clientY - viewportY) * internalHeight / viewportHeight
```

The intended [`Present`](Project/native/graphics/swapchain_hook.cpp) path has seven coordinated stages:

1. Start the high-resolution frame timer.
2. Sample VRAM pressure and apply safe budget policies.
3. Process a bounded amount of queued upload work (design target: up to 2.5 MiB).
4. Apply dirty-state projection updates through the dynamic ring buffer.
5. Render the adapted HUD and interface layers.
6. Reset the frame's linear arena after its transient CPU data is no longer needed.
7. Apply frame pacing and forward the call to the original swapchain `Present` function.

### Build from Source

From the repository root, run the project's PowerShell build and packaging scripts:

```powershell
powershell -ExecutionPolicy Bypass -File .\Project\tools\Build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File .\Project\tools\Package.ps1
```

Source locations referenced by the architecture include `Project/include/unc/`, `Project/vendor/`, and `Project/native/graphics/`. **Build prerequisites, script availability, and the output package should be confirmed against the checked-out revision.**

---

## Testing and Feedback

Useful reports include:

1. The exact game/build and project revision tested.
2. Windows version, CPU, GPU, driver, and system RAM.
3. Graphics API, resolution, aspect ratio, and active quality settings.
4. Average FPS, frame-time captures or 1% lows, plus VRAM/RAM observations.
5. Steps to reproduce, relevant logs, and before/after comparisons when possible.

Constructive reports help distinguish true engine-side improvements from driver behavior, scene differences, or misleading one-frame readings.

## Frequently Asked Questions

**Is this an official UNCHARTED PC release?**  
No. It is an independent community engineering initiative.

**Does this repository include the original games or copyrighted assets?**  
No game content is provided or authorized by this README. Users remain responsible for obtaining and using any required software lawfully.

**Will every 1 GB GPU achieve 30–60 FPS?**  
No. That is a research and optimization objective, not a proven compatibility claim.

**Does setting an 820 MiB budget guarantee zero memory paging?**  
No. Actual GPU residency is affected by the driver, operating system, shared memory, other processes, and the game's resource behavior.

## Legal and Attribution

UNCHARTED and related names, characters, and game assets are the property of their respective rights holders. This project is **not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment, PlayStation, Naughty Dog, or Bluepoint Games**.

This repository documents independent compatibility and performance engineering. It does not grant rights to redistribute proprietary game files.

---

**Sic Parvis Magna.** Small beginnings. Ambitious engineering. Measurable progress.
