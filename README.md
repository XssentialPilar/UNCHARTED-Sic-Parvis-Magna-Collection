# UNCHARTED: Sic Parvis Magna Collection

> **Sic Parvis Magna** — *Greatness from small beginnings.*

[![Downloads](https://img.shields.io/badge/Downloads-GitHub_Releases-C69D60?style=flat-square)](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases)
![Platforms](https://img.shields.io/badge/Platforms-Windows_%7C_Linux_Development-3474A6?style=flat-square)
![Status](https://img.shields.io/badge/Status-Active_Development-455E56?style=flat-square)
![Technology](https://img.shields.io/badge/Technology-Native_C%2B%2B-00599C?style=flat-square)

**An independent PC compatibility and performance project dedicated to making Nathan Drake's adventures more accessible, customizable, and enjoyable across different generations of hardware.**

UNCHARTED: Sic Parvis Magna Collection combines a dedicated launcher, keyboard and mouse improvements, adaptable display options, and low-level runtime engineering. Our goal is straightforward: give players greater control over their experience while preserving the atmosphere and identity of the original games.

This is an **unofficial community project**, not an official PC release. **Original game files are not included.**

## Download and Get Started

**[Downloads, installation details, and release notes](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases)**

Packages are distributed by operating system and processor architecture. Please choose the package matching your computer; a launcher-only or experimental build should not be mistaken for a complete playable package.

1. Download the appropriate package from **Releases**.
2. Extract it to a folder of your choice, keeping its internal structure intact.
3. Open the launcher included with your package. On supported Windows packages, the native entry point is `Start Uncharted.exe`; an alternative launcher may also be provided.
4. Select your own compatible game-file directory, configure your preferences, and choose the adventure you want to launch.

Download sizes, supported packages, fixes, and platform-specific instructions belong to each Release page. This main README intentionally contains no version-specific information.

## Built Around the Player

| Experience | What it offers |
| :--- | :--- |
| **Explorer's Desk** | A central place to manage games, graphics, displays, controls, language, and update checks. |
| **Keyboard and mouse support** | Clear, vector-derived prompts designed around real key proportions instead of stretched controller artwork. |
| **Flexible display options** | Adaptation work for 4:3, 16:9, 16:10, 21:9 and other resolutions, with attention to correct framing and UI proportions. |
| **Multiple window modes** | Fullscreen, borderless window, and standard window operation where supported. |
| **Configurable graphics** | Texture, shadow, memory, and related quality controls intended to offer meaningful performance tradeoffs. |
| **Hardware-aware operation** | Detection of available graphics adapters, with automatic and user-directed selection where supported by the active rendering backend. |
| **Performance engineering** | Continued work on frame pacing, shader preparation, visibility, streaming, and memory efficiency. |
| **Platform-aware updates** | Update channels designed to match the correct operating system and architecture while preserving user settings. |

## Windows and Linux Compatibility

**Windows 7 SP1, Windows 8, and Windows 8.1 are included in the project's native compatibility work in some versions**, alongside Windows 10 and Windows 11. Dedicated launcher builds and shared-library components are being developed for both **32-bit (x86)** and **64-bit (x64)** Windows where applicable.

It is important to distinguish **launcher compatibility** from **playable game-runtime compatibility**:

| Platform | Project coverage | Current qualification |
| :--- | :--- | :--- |
| **Windows 10 / 11, x64** | Native launcher, shared runtime, and game execution development | Primary Windows gameplay and testing targets; consult each Release for tested builds. |
| **Windows 7 SP1 / 8 / 8.1, x64** | Native launcher and legacy runtime compatibility work | Experimental builds; full gameplay and legacy OS validation remain ongoing. |
| **Windows x86 (32-bit)** | Native launcher, configuration, and supporting tools | The existing 64-bit game runtime cannot run directly on a 32-bit Windows host. |
| **Linux x64** | Native C++/SDL3, ELF runtime components, and Vulkan-based development | Experimental; native gameplay requires further real-hardware verification. |
| **Linux x86** | Feasibility research | Not a verified playable target. |

All native Linux work is intended to run without Wine or Proton. An available download does **not** automatically mean that gameplay has been verified on the listed platform. The relevant Release notes provide the current package status and limitations.

## Hardware Reference Profiles

The following are **engineering and optimization targets**, not certified requirements or guaranteed frame rates. Actual results vary with game build, drivers, scene complexity, system memory, and graphics capabilities.

| Component | Minimum reference — 720p / 30 FPS target | Recommended reference — 1080p / 60 FPS target |
| :--- | :--- | :--- |
| **Processor** | Intel Core 2 Quad Q8400 / AMD Phenom II X4 945, or comparable | Intel Core i5-2400 / AMD FX-6300, or comparable |
| **System RAM** | 4 GB | 6–8 GB |
| **NVIDIA GPU reference** | GTX 460 / 550 Ti / 650 / 750, 1 GB class | GTX 750 Ti / 660, 2 GB class |
| **AMD GPU reference** | Radeon HD 5770 / 6790 / 7750, 1 GB class | Radeon HD 7850 / R7 265 / R9 270, 2 GB class |
| **Physical VRAM reference** | 1 GB | 2 GB |
| **Project-managed VRAM target** | Up to 820 MiB | Up to 1,720 MiB |
| **Storage** | Approximately 40 GB for compatible game files, depending on the build | Similar; SSD preferred |
| **Graphics API** | Compatible Direct3D or Vulkan backend, as supported by the operating system, GPU, and driver | Same, with greater performance headroom |

An early individual test reported approximately **34 FPS average on integrated graphics with a reported VRAM allocation of 512 MB or less**. This is an encouraging observation, **not** a new minimum requirement or a reproducible cross-hardware benchmark. Integrated GPUs may use additional shared system memory, and detailed test conditions are needed for direct comparisons.

## The Technology Behind the Experience

### Shared Native Architecture

The project is moving toward a common C++ codebase with platform-specific components. **SPMCore** provides a foundation for configuration, hardware information, runtime coordination, and other shared services. Windows uses native DLL builds; Linux uses native shared libraries (`.so`). The native Explorer's Desk interface is being developed using C++ and SDL3, with existing working launchers retained during the transition.

The goal is a consistent user experience without mixing Windows/Linux binaries or x86/x64 dependencies. Package updates are designed around distinct platform identifiers so that an update for one system is not offered to an incompatible installation.

### Rendering, VRAM, and Frame Pacing

The [VRAM budget manager](Project/include/unc/graphics/vram_budget_mgr.hpp) is designed to respond to memory pressure using texture-detail selection, safe resource eviction, and streaming controls. [Transient render-target reuse](Project/include/unc/graphics/transient_rt_pool.hpp) and visibility-aware rendering seek to reduce unnecessary GPU work without discarding resources still needed by the scene.

At 60 FPS, the frame-time budget is approximately **16.67 ms**. The [frame pacing system](Project/include/unc/core/frame_pacer.hpp) and related scheduling work prioritize consistency and fewer avoidable stalls. Benchmark objectives are **not guarantees** for every device or scene.

### Display and Input Adaptation

Correct aspect-ratio support requires more than changing the output resolution. The camera, visible scene area, HUD, and pointer coordinates must remain aligned. The [input mapper](Project/include/unc/input/input_mapper.hpp) handles independent horizontal and vertical viewport transformations, while PC-specific key artwork preserves the distinct dimensions of keys such as Tab, Shift, and Backspace.

### Source and Development

- [Runtime architecture](docs/ARCHITECTURE.md)
- [Input pipeline](docs/INPUT_PIPELINE.md)
- [Performance and VRAM budgets](docs/PERFORMANCE_BUDGETS.md)
- [Graphics sources](Project/native/graphics/)
- [Project headers](Project/include/unc/)

For development builds on a suitable Windows toolchain:

```powershell
powershell -ExecutionPolicy Bypass -File .\Project\tools\Build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File .\Project\tools\Package.ps1
```

Build prerequisites, CI pipelines, and available targets depend on the development environment. Consult the source tree and workflow definitions for platform-specific details.

## Development, Support, and Feedback

This project remains under active development. Native Linux gameplay, legacy Windows verification, complete launcher feature parity, additional rendering improvements, and wider hardware validation are ongoing areas of work. Some graphics settings, display modes, or UI adaptations may behave differently across devices and builds.

If you experience an issue, please [open a GitHub Issue](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/issues) with your operating system, CPU/GPU, game build, display settings, steps to reproduce, and any useful logs or screenshots.

Thank you for helping us turn promising engineering work into a dependable experience for more players.

## Legal and Attribution

UNCHARTED and related trademarks, characters, and original game assets belong to their respective rights holders. This project is **not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment, PlayStation, Naughty Dog, or Bluepoint Games**. It does not include or grant permission to distribute proprietary game files.

---

**Sic Parvis Magna.** *Greatness from small beginnings.*
