# UNCHARTED: Sic Parvis Magna Collection — Technical Reference

**Version:** `v0.5.0-beta` · **Status:** Public Beta · **Primary platform:** Windows x64

This document covers the runtime architecture, display and input transformation, memory-management policies, frame pacing, packaging, and validation expectations for the first public Beta. The [main README](../README.md) is intentionally written for players; this document is intended for developers, testers, and technical contributors.

> **Scope of claims:** Release notes describe features introduced in v0.5.0 Beta. They are not a substitute for hardware-specific regression tests. Legacy hardware profiles and strict frame-time thresholds are engineering targets unless accompanied by reproducible measurements.

## 1. Runtime and Delivery Architecture

The distribution combines a native runtime/presentation layer with a dedicated Windows launcher.

| Component | Role | Reference |
| --- | --- | --- |
| **Explorer's Desk** | User configuration, game selection, display/graphics controls, localization, GPU selection, and release update checks | Release launcher (`.NET 10`, self-contained) |
| **Native runtime** | Game execution and integration with the graphics, input, and presentation paths | `Uncharted.exe` / `gow3-probe.exe` |
| **UI prompt assets** | Prebaked vector-derived keyboard/mouse prompts and physical key proportions | `gow3_prompt_assets.bin` |
| **Supporting runtime files** | SDL3 and video/audio codec dependencies shipped in the complete package | `SDL3.dll` and packaged dependencies |
| **Graphics abstraction** | Rendering, memory telemetry, transient surfaces, and frame presentation | [`Project/native/graphics/`](../Project/native/graphics/) |
| **Build and package pipeline** | Native compilation, tests, asset processing, and release assembly | [`Project/tools/`](../Project/tools/) |

The **complete ZIP** is the expected end-user installation. The separate launcher executable does not by itself provide a complete runtime environment.

### Distribution and provenance

- Release tag: [`v0.5.0-beta`](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/tag/v0.5.0-beta) (GitHub prerelease).
- Main package: `UNCHARTED-Sic-Parvis-Magna-Collection-v0.5.0-Beta.zip` (`203,434,907` bytes; approximately `194.01 MiB`).
- Launcher-only executable: `Uncharted-Sic-Parvis-Magna-Launcher.exe` (`68,145,892` bytes; approximately `64.99 MiB`).
- Both assets are published on the tag's official GitHub Release page.

Original proprietary game files are not part of this distribution. The user must provide a compatible, lawfully obtained game directory. The underlying port documentation identifies a `CUSA02344` folder containing `eboot.bin` and `sce_sys`; actual supported titles/builds should be validated against the packaged runtime.

## 2. Rendering Backends and Display Adaptation

The project includes graphics integration work around Direct3D and Vulkan paths, with separate backend capabilities and driver requirements. The release notes mention Vulkan depth-clamp changes; this must not be interpreted as automatic compatibility with every D3D11-class or legacy GPU.

### 2.1 Swapchain, viewport, and aspect ratio

The display implementation must distinguish four coordinate spaces:

1. **Game/internal render dimensions** — the scene render target and its projection.
2. **Presentation dimensions** — the host surface or swapchain.
3. **Visible viewport** — the portion of the surface occupied by the active game image.
4. **UI-safe region** — the space in which HUD elements, prompts, and text are laid out.

The aspect ratio must derive from the active viewport:

```text
aspectRatio = viewportWidth / viewportHeight
```

Input is transformed independently on each axis:

```text
gameX = (clientX - viewportX) * internalWidth  / viewportWidth
gameY = (clientY - viewportY) * internalHeight / viewportHeight
```

For 4:3, 16:9, 16:10, and 21:9, validation must distinguish **true scene extension** from a stretched image or a cropped presentation. Removing black bars at the presentation stage does not, by itself, prove that additional scene geometry is correctly visible. Camera projection, frustum visibility, overlay anchors, subtitle baselines, and interaction coordinates must all agree.

Supported window modes described by the release are Fullscreen, Borderless Window, and Windowed. Verify both mode transitions and resolution changes without duplicating scaling transforms.

See [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) and [`docs/INPUT_PIPELINE.md`](INPUT_PIPELINE.md) for the project's architectural specifications.

### 2.2 Keyboard and mouse prompt geometry

The Beta uses the `UNCHARTED_KEYBOARD_MOUSE_EN_V4` vector asset family and packages game-facing prompt resources into `gow3_prompt_assets.bin`. The intended display path is:

```text
Input binding → semantic prompt ID → key geometry and physical U size
              → anchored/scaled HUD quad → final composited UI
```

Important invariants:

- 1U keys retain their natural proportions; Tab, Shift, Backspace, Enter, and Space use their own width/height metadata.
- A prompt replacement must remove or suppress the corresponding legacy PS4 container, background, touchpad/hand art, and any secondary geometry that would clip the new key.
- The glyph, background, interactive bounds, and UI anchor must be scaled coherently; scaling only the replacement texture leaves legacy framing defects.
- Prompt composition must remain stable under display-mode, language, and aspect-ratio changes.

The release notes cite `gow3_prompts.cpp` for runtime prompt cleanup. Verify the final compilation/package path and the rendered output rather than assuming that asset replacement alone completes the conversion.

### 2.3 Lighting and depth-path changes

The v0.5.0 release notes report a Vulkan graphics-pipeline update using `depthClampEnable` to address sharply clipped or triangular light shafts. They also describe `DepthFeedbackGeneral` handling for depth-feedback hazards.

Treat these as backend-specific interventions requiring visual regression testing. In particular:

- Compare affected scenes before and after the depth-state change.
- Confirm correct intersection of volumetric beams with walls, nearby objects, and the camera.
- Test fog/transparency, near-plane intersections, shadow receivers, and moving lights.
- Verify that changes do not hide geometry, break depth testing, or cause driver-specific failures.

## 3. Graphics Quality Must Change Actual Work

The launcher may expose a value without successfully changing the live renderer. For each quality control, a passing test requires a measurable effect on the relevant rendering path.

| Control | Expected implementation effect | How to verify |
| --- | --- | --- |
| **Shadow quality** | Change shadow-map resolution, active cascade budgets, and/or shadow distance | Capture the actual allocations and shadow rendering passes |
| **Dynamic shadows** | Enable/disable eligible dynamic shadow passes without breaking essential lighting | Compare pass count, visual output, and frametime |
| **Texture quality** | Change resident mip levels, texture detail, and streaming priorities | Inspect residency and usage under identical scenes |
| **Anisotropic filtering** | Update actual sampling configuration | Check sampler state and oblique-texture detail |
| **Render distance** | Change draw/streaming visibility with controlled transition behavior | Count submitted draws and examine pop-in/frametimes |
| **FPS cap / pacing** | Update frame timing policy and present behavior | Capture frametime distribution over a sustained run |

Do not equate lower menu values with lower GPU cost until measurements confirm the reduction.

## 4. VRAM Telemetry and Residency Management

The source contains [`VramBudgetManager`](../Project/include/unc/graphics/vram_budget_mgr.hpp), [`TransientRenderTargetPool`](../Project/include/unc/graphics/transient_rt_pool.hpp), and related structures for tracking and controlling resources.

### 4.1 Memory-pressure policy

For a positive active memory budget, define:

```text
usageRatio = currentManagedUsageBytes / activeBudgetBytes
```

| Pressure tier | Threshold | Intended response |
| --- | --- | --- |
| Normal | Below `0.80` | Permit appropriate high-detail mip residency |
| Elevated | `0.80` to below `0.88` | Restrict nonessential high-detail promotions |
| Critical | `0.88` and above | Prefer safe mip downgrades, eviction candidates, and reclaimable transient surfaces |

Historical reference profiles define engine-managed budgets of `859,832,320` bytes (`820 MiB`) for 1 GiB VRAM cards and `1,803,550,720` bytes (`1,720 MiB`) for 2 GiB cards. **These are target limits for managed resources, not guarantees of total driver/device usage or zero PCIe paging.** Actual residency depends on the graphics backend, operating system, driver, other processes, and game resources.

The texture strategy explores BC1–BC7 block compression, low-detail mip-tail residency, LRU-like promotion/demotion decisions, and bounded upload work. A claimed 75% saving when dropping a top mip is a theoretical texture-size relationship, not a guaranteed whole-game VRAM reduction.

### 4.2 Visibility, streaming, and resource lifetimes

Frustum and occlusion culling can reduce unnecessary draw submission. Portal/room visibility is particularly relevant when an interior is fully obscured by an exterior wall.

However, **not rendered** does not automatically mean **safe to delete from memory**. Residency changes must respect active command buffers, fences, resource references, and the possibility of rapid camera return. Predictive, proximity-aware streaming and a short reuse window help avoid repeated uploads and stalls.

Transient render-target reuse likewise requires nonoverlapping lifetimes and compatible formats, dimensions, and synchronization.

## 5. Frame Pacing and Performance Measurement

[`FramePacer`](../Project/include/unc/core/frame_pacer.hpp) defines a timing interface for target FPS, measured frame time, and jitter. The framework uses high-resolution timing and a hybrid wait strategy; its design target is a stable cadence, not merely peak throughput.

At 60 FPS:

```text
targetFrameTime = 1000 / 60 = 16.6667 ms
```

The project's `< 0.45 ms` frame-to-frame jitter figure is an aggressive **acceptance target under controlled conditions**, not a universally guaranteed runtime invariant.

Recommended reporting for each representative scene:

- Average FPS and 1% low FPS.
- Median, 95th-percentile, and 99th-percentile frame times.
- CPU and GPU utilization, temperatures, clocks, and driver version.
- Resident/allocated VRAM, system RAM, and any nonlocal/shared-memory usage.
- Shader-cache state, graphics preset, resolution, API, and frame cap.
- Scene-transition traces and before/after comparisons with matching test conditions.

Single screenshots do not establish sustained frame pacing or absence of stutter.

## 6. Launcher, Configuration, and Updates

Explorer's Desk is distributed as a self-contained Windows launcher. The release describes:

- Selection of GPU, graphics options, screen mode, resolution, and aspect ratio.
- Automatic OS-language detection across 10 languages with manual override.
- Persistent per-user/per-game settings and input mappings.
- Update checks against the project's official GitHub Releases.

**Update integrity expectations:** identify a newer tagged release, download only official assets, verify download integrity when hashes are provided, preserve user configuration, and recover gracefully from interrupted updates. Configuration persistence should be regression-tested; it is not enough to verify that a settings file exists before an update.

## 7. Build, Testing, and Packaging

From the repository root, use the existing scripts:

```powershell
powershell -ExecutionPolicy Bypass -File .\Project\tools\Build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File .\Project\tools\Package.ps1
```

The build script configures native compilation, shader preparation, launcher artifacts, and regression targets; inspect the checked-out revision for prerequisites and generated outputs. Do not assume historical hardware targets imply successful build or runtime compatibility on that platform.

### Suggested Beta validation matrix

| Area | Minimum regression coverage |
| --- | --- |
| Display | 4:3, 16:9, 16:10, 21:9; fullscreen/borderless/windowed; live mode changes |
| UI | Menu, HUD, combat prompts, journal, subtitles, map/pointer alignment |
| GPU | Integrated and discrete adapters; low- and high-memory profiles; backend capability checks |
| Graphics quality | Lowest/highest shadows and textures; dynamic shadows off/on; observable resource differences |
| Streaming | Interior/exterior transitions, fast camera turns, repeated area traversal |
| Stability | Extended play, shader-cache cold/warm launches, suspend/focus changes |
| Updater | Available/no update, interrupted download, preserved settings and mappings |

Track **Implemented**, **Tested**, **No Regression**, and **Passed** separately for each release requirement. A compiled feature is not automatically a verified feature.

### Technical source references

- [Architecture](ARCHITECTURE.md)
- [Input pipeline](INPUT_PIPELINE.md)
- [Performance and VRAM budgets](PERFORMANCE_BUDGETS.md)
- [Performance observations](../LATEST_PERFORMANCE_REPORT.md)
- [Windows runtime notes](../Project/tools/README-Windows.txt)

## 8. Beta Limitations and Reporting

First-launch shader preparation can take extra time. Performance varies by title, game revision, GPU, driver, rendering backend, scene complexity, and configuration. Historical 1 GiB/2 GiB VRAM targets are experimental until separately tested; Vulkan 1.3 capability must not be assumed on older graphics cards.

Useful issue reports should include the exact release/build, game title and version, Windows version, GPU and driver, graphics API, display mode, resolution, active settings, steps to reproduce, and relevant logs or frame-time captures.

Please report reproducible problems through the [GitHub issue tracker](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/issues).

## Legal and Attribution

This is an independent, unofficial compatibility and performance project. It is not affiliated with or endorsed by Sony Interactive Entertainment, PlayStation, or Naughty Dog. Original game content and related trademarks remain the property of their respective owners. Users must supply game files they are legally entitled to use.

---

*Thank you to everyone testing, reporting issues, and helping improve the project.*
