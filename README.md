# UNCHARTED: Sic Parvis Magna Collection

> **Sic Parvis Magna** — *Greatness from small beginnings.*

[![Release](https://img.shields.io/badge/Release-v0.5.0_Beta-C69D60?style=flat-square)](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/tag/v0.5.0-beta)
![Platform](https://img.shields.io/badge/Platform-Windows-3474A6?style=flat-square)
![Development](https://img.shields.io/badge/Status-Active_Development-455E56?style=flat-square)
![Technology](https://img.shields.io/badge/Technology-C%2B%2B20%20%2F%20C%2B%2B23-00599C?style=flat-square)

**An independent PC compatibility and performance project built to make Nathan Drake's adventures more adaptable, accessible, and enjoyable across different generations of hardware.**

UNCHARTED: Sic Parvis Magna Collection brings together a dedicated launcher, keyboard and mouse interface improvements, expanded display options, and low-level performance engineering. The aim is to give players more control over how their experience looks, feels, and runs—while preserving the character of the original games.

This is an **unofficial community project**, not an official PC release. **Original game files are not included.**

## Get Started

### [Download the Complete v0.5.0 Beta Package](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/download/v0.5.0-beta/UNCHARTED-Sic-Parvis-Magna-Collection-v0.5.0-Beta.zip)

**Recommended for new users · ZIP · Approximately 194 MB**

The complete package contains the **Explorer's Desk launcher**, native runtime components, keyboard and mouse prompt resources, and the supporting files distributed with this release.

1. Download and extract the complete ZIP to a folder of your choice.
2. Open `Uncharted - Sic Parvis Magna Collection.exe`.
3. Choose your compatible, legally obtained game directory, configure your preferences, and launch.

Already have the required runtime? The [standalone launcher](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/download/v0.5.0-beta/Uncharted-Sic-Parvis-Magna-Launcher.exe) is also available (approximately 65 MB). **It is not a complete first-time installation.**

## Designed Around the Player

| Experience | What it brings to PC |
| :--- | :--- |
| **Explorer's Desk** | One place to manage your game directory, graphics, resolution, display mode, language, and update checks. |
| **Keyboard and mouse interface** | Crisp, vector-derived key prompts with dimensions appropriate to each key, including larger keys such as Tab and Backspace. |
| **Flexible displays** | Adaptations for 4:3, 16:9, 16:10, and 21:9, designed to use available screen space without stretching characters or interface elements. |
| **Display modes** | Fullscreen, Borderless Window, and Windowed operation. |
| **Scalable graphics** | Shadow and texture settings intended to affect real rendering workloads, giving players a useful quality/performance choice. |
| **Performance-minded runtime** | Work on frame pacing, memory residency, streaming, and rendering efficiency to improve consistency—not merely momentary peak FPS. |
| **Continued development** | Public Beta feedback and launcher update checks help guide future improvements. |

### A Small Beginning, a Meaningful Result

**An early test reported an average of 34 FPS using CPU-integrated graphics with 512 MB of VRAM or less shown in the system's configuration or telemetry.**

That result is encouraging because the project has primarily targeted hardware with **1 GB or 2 GB of VRAM**. It suggests that some configurations below the original memory reference may still be able to run the game.

**What the result does—and does not—mean:**

- **34 FPS is a reported average from a particular test**, not a guaranteed frame rate or an established minimum specification.
- Integrated graphics can draw on **shared system memory**. A reported 512 MB allocation does not necessarily represent all memory accessible to the GPU.
- The exact GPU/CPU model, game build, resolution, graphics settings, driver, and frame-time distribution should accompany a reproducible benchmark before broader compatibility claims are made.

We believe performance improvements should be judged by **how steadily the game plays**, not by a single impressive number.

## Hardware Reference Profiles

These preserve the project's **original minimum and recommended engineering profiles**. They are **development and testing targets**, not a certified compatibility list. The new 512 MB integrated-graphics observation is an *experimental result below the reference minimum*; it does not replace the baseline.

| Component | Minimum reference — 720p / 30 FPS target | Recommended reference — 1080p / 60 FPS target |
| :--- | :--- | :--- |
| **Operating system** | Windows 10/11, 64-bit | Windows 10/11, 64-bit |
| **Processor** | Intel Core 2 Quad Q8400 / AMD Phenom II X4 945 (or comparable CPU) | Intel Core i5-2400 / AMD FX-6300 (or comparable CPU) |
| **System memory** | 4 GB RAM | 6–8 GB RAM |
| **NVIDIA graphics reference** | GTX 460 / GTX 550 Ti / GTX 650 / GTX 750, 1 GB class | GTX 750 Ti / GTX 660, 2 GB class |
| **AMD graphics reference** | Radeon HD 5770 / HD 6790 / HD 7750, 1 GB class | Radeon HD 7850 / R7 265 / R9 270, 2 GB class |
| **Physical VRAM reference** | 1 GB | 2 GB |
| **Project-managed VRAM target** | Up to 820 MiB | Up to 1,720 MiB |
| **Storage estimate** | Approximately 40 GB for the relevant game files; depends on the build | Same estimate; an SSD is preferable |
| **Graphics capability** | Compatible Direct3D 11 feature-level or Vulkan backend, as supported by the runtime and driver | Same, with sufficient performance headroom |

**Important compatibility details**

- The **current launcher is intended for modern 64-bit Windows**. Windows 7 SP1 and Windows 8.1 were historical research targets, **not verified supported systems for this Beta**.
- The listed CPU and GPU families describe **performance research classes**, not machines on which the release has necessarily been tested.
- A GPU's advertised DirectX support does not guarantee the required DXGI interfaces or Vulkan features. In particular, **Vulkan 1.3 should not be assumed available on older GPUs**.
- The **820 MiB / 1,720 MiB limits** describe engine-managed budget targets. They do not cap every allocation made by the game, operating system, or graphics driver.
- A 30 or 60 FPS goal is **not a promise of a locked frame rate**. Results depend on the scene, game build, driver, available RAM/VRAM, and graphics settings.

## The Engineering Behind the Experience

The framework uses **C++20/C++23** and focuses on measurable improvements in the rendering, presentation, memory, and input paths.

### Efficient Memory Use

The [VRAM budget manager](Project/include/unc/graphics/vram_budget_mgr.hpp) is designed around pressure-aware resource residency. Its reference policy distinguishes normal usage (below **80%**), elevated pressure (**80–88%**), and critical pressure (**88% and above**). Texture mip selection, safe eviction, and [transient render-target reuse](Project/include/unc/graphics/transient_rt_pool.hpp) can help reduce unnecessary memory pressure when the active backend supports them.

**Visibility is not the same as residency:** objects hidden by walls should avoid unnecessary drawing where culling is effective, but resources must only be released after they are no longer needed by the GPU or an imminent scene transition.

### Consistent Frame Delivery

At **60 FPS**, each frame has approximately **16.67 ms** to complete. The [frame pacing system](Project/include/unc/core/frame_pacer.hpp) targets more predictable delivery, while streaming queues and per-frame allocation policies aim to reduce avoidable stalls. The project's **sub-0.45 ms jitter** figure is a controlled-test engineering objective, **not a universal guarantee**.

### Display and Input Adaptation

Different screen shapes require coordinated camera projection, scene framing, UI placement, and pointer mapping. The [input mapper](Project/include/unc/input/input_mapper.hpp) independently adjusts horizontal and vertical coordinates to follow the active viewport. Changing the output aspect ratio alone is insufficient: the game must expose the appropriate scene area and preserve UI proportions.

The input presentation work also replaces legacy controller-oriented prompt artwork with PC-appropriate keyboard and mouse geometry. Larger keys retain their distinct dimensions rather than being stretched into a uniform square.

### Code and Documentation

- [Runtime architecture](docs/ARCHITECTURE.md)
- [Input pipeline](docs/INPUT_PIPELINE.md)
- [Performance and VRAM budgets](docs/PERFORMANCE_BUDGETS.md)
- [Native graphics sources](Project/native/graphics/)
- [Project headers](Project/include/unc/)

To build from a suitable Windows development environment, the repository provides:

```powershell
powershell -ExecutionPolicy Bypass -File .\Project\tools\Build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File .\Project\tools\Package.ps1
```

Build requirements and available targets depend on the checked-out revision.

## Current Status and Feedback

**v0.5.0 Beta is an early public release.** Features and compatibility may differ across game builds, graphics APIs, drivers, and hardware. Visual artifacts, incomplete prompt replacements, or uneven performance may still occur.

If something does not work as expected, please [open an issue](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/issues) with your game/build, system specifications, graphics settings, reproduction steps, and—where available—logs, screenshots, or frame-time data.

Your feedback helps turn promising results into reliable improvements. Thank you for testing the project and supporting its development.

## Frequently Asked Questions

**Is this an official PC release?**  
No. This is an independent community compatibility and performance project.

**Are the original games included?**  
No. You must supply compatible game files that you are legally entitled to use.

**Does the 34 FPS integrated-graphics result mean 512 MB is now the minimum requirement?**  
No. It is an encouraging individual test result, not a verified minimum hardware specification.

**Will all supported resolutions, GPUs, and graphics settings perform identically?**  
No. The project is actively being tested and refined; actual behavior varies with the hardware and game configuration.

## Legal and Attribution

UNCHARTED and related trademarks, characters, and original game assets belong to their respective rights holders. This project is **not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment, PlayStation, Naughty Dog, or Bluepoint Games**. It does not grant permission to distribute proprietary game files.

---

**Sic Parvis Magna.** Greatness from small beginnings.
