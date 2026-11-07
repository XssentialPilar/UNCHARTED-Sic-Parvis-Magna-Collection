# UNCHARTED: Sic Parvis Magna Collection
### The Definitive Native PC Port & Performance Engineering Suite

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20(64--bit)-0078D6?style=for-the-badge&logo=windows)](https://github.com/)
[![Architecture](https://img.shields.io/badge/Architecture-Native%20x86--64%20%7C%20SPM%20Core-E5C158?style=for-the-badge)](https://github.com/)
[![Graphics](https://img.shields.io/badge/Backends-Direct3D%209Ex%20%7C%20D3D11%20%7C%20D3D12%20%7C%20Vulkan-228B22?style=for-the-badge)](https://github.com/)
[![VRAM Target](https://img.shields.io/badge/VRAM%20Target-820%20MB%20Strict%20Cap%20to%20Unlimited-D32F2F?style=for-the-badge)](https://github.com/)
[![Status](https://img.shields.io/badge/Status-Gold%20Master%20Ready-gold?style=for-the-badge)](https://github.com/)

> *"Sic Parvis Magna" — Greatness from small beginnings.*  
> — Sir Francis Drake

Welcome to **UNCHARTED: Sic Parvis Magna Collection**, the definitive standalone PC runtime architecture and restoration project for the complete Nathan Drake trilogy: **Uncharted: Drake's Fortune**, **Uncharted 2: Among Thieves**, and **Uncharted 3: Drake's Deception**.

Engineered from the ground up for modern and legacy Windows hardware, this collection bridges low-level console reverse engineering with a custom native graphics core, deterministic frame pacer, and flexible multi-API presentation pipeline. Whether running on a high-end RTX desktop, an ultra-portable gaming handheld, or a 1 GB VRAM integrated CPU, *Sic Parvis Magna* delivers fluid 60+ FPS gameplay with unmatched fidelity, instant response, and surgical memory control.

---

## 🧭 The Trilogy: Three Legendary Expeditions, One Unified Experience

```
╔═════════════════════════════════════════════════════════════════════════════════════════╗
║                                 SIC PARVIS MAGNA COLLECTION                             ║
╠══════════════════════════╦══════════════════════════════╦═══════════════════════════════╣
║    PARTE I: DRAKE'S      ║       PARTE II: AMONG        ║      PARTE III: DRAKE'S       ║
║        FORTUNE           ║           THIEVES            ║          DECEPTION            ║
╠══════════════════════════╬══════════════════════════════╬═══════════════════════════════╣
║  • El Dorado Expeditions ║  • Marco Polo's Lost Fleet   ║  • Atlantis of the Sands      ║
║  • Lush Jungle Foliage   ║  • Himalayan Mountain Passes ║  • Rub' al Khali Dunes        ║
║  • Revamped Water Shader ║  • Runaway Train Dynamics    ║  • Château Inferno Sequence   ║
║  • Low-latency Aiming    ║  • Volumetric Blizzard FX    ║  • High-frequency Geometry    ║
╚══════════════════════════╩══════════════════════════════╩═══════════════════════════════╝
```

1. **Uncharted: Drake's Fortune (Part I):** Search for the lost treasure of El Dorado on a forgotten island. Features optimized procedural foliage LOD, refined wet-surface shaders, and rock-solid single/multithreaded command rendering.
2. **Uncharted 2: Among Thieves (Part II):** Journey across Nepal and the mountains of Tibet to discover the mythical Shambhala. Optimized for massive structural vistas, dynamic snow particle systems, and intense set-pieces.
3. **Uncharted 3: Drake's Deception (Part III):** Uncover the secrets of the Iram of the Pillars through the French châteaux, Syrian citadels, and the vast Arabian desert. Built with robust tiled-lighting handling and advanced physics memory residency.

---

## ✨ Player-Centric Features & Commercial Highlights

### 🎮 Modern C# / .NET 10 Explorer's Desk Launcher
* **Unified Expedition Hub:** Launch any chapter of the trilogy directly from a bespoke explorer-themed desk interface with rich artwork, historical journals, and persistent sound design.
* **Instant GPU Adapter Selector:** Switch directly between Dedicated GPUs (e.g. NVIDIA RTX) and Integrated CPUs (e.g. AMD Radeon 760M / Intel Iris Xe) straight from the main screen, with automatic hardware-tailored presets.
* **One-Click Graphical Profiles:**
  * **iGPU / 820 MB Mode:** Tailored for integrated graphics and laptops with limited shared memory.
  * **Bajo (1 GB VRAM):** Strict memory budget, 32-tap shadows, and adaptive mip streaming.
  * **Medio (2 GB VRAM):** Balanced 1080p 60 FPS presentation with native post-processing.
  * **Alto & Ultra (4K / Enthusiast):** Full-resolution textures, high-precision cascade shadows, and unconstrained streaming pools.

### 🖼️ Total Shadow Mastery & Dynamic Shadow Control
* **Toggleable Dynamic Shadows (`dynamic_shadows=0/1`):** Complete freedom to turn off costly directional sun passes and soft shadow filters for massive frame-rate boosts on budget hardware, while maintaining rich ambient lighting and baked world shading.
* **32-Tap Fast Shadow Pass:** Optimized shadow filtering pass down from 256 samples, delivering crisp shadows with an 8x reduction in GPU ALU load.

### 🔭 Adaptive LOD & Draw Distance Scaling
* **Granular Distance Scales:** Choose between **Muy baja (50%)**, **Baja (75%)**, **Media (100%)**, **Alta (125%)**, **Ultra (150%)**, or **Automática (Adaptativa por VRAM)**.
* **Progressive Exponential Smoothing:** Dynamic distance transitions smoothly in runtime (`current += (target - current) * 0.25`), completely preventing sudden geometry popping and stuttering during rapid camera sweeps.

### ⚡ True Native Multi-Backend Rendering
* **Direct3D 9 / 9Ex (FlipEx Nativo):** Authentic hardware-level D3D9 backend operating independently without Vulkan or D3D11 translation. Perfect for lightweight GPUs and low-latency presentation.
* **DirectX 11 (Hardware Flip-Discard):** Optimal low-overhead presentation path with asynchronous swapchain fences.
* **DirectX 12 & Vulkan 1.3:** Next-generation modern backends supporting robust descriptor indexing and raw compute submission.

### 🎯 Seamless In-Game Overlay & Input Nirvana
* **In-Game Management Hub (`Insert` or `LT + RS`):** Adjust display, performance, image scaling, overlay metrics, and keybindings live without ever leaving the action.
* **Raw Mouse Look:** Zero-smoothing mouse aiming with decoupled X/Y sensitivity, automatic stick-zone linearization, and configurable aim multipliers.
* **Universal Controller Support:** Native plug-and-play for PlayStation DualSense / DualShock 4 (with authentic glyphs), Xbox controllers, and DirectInput devices.
* **Persistent Settings Guarantee:** Control bindings, mouse preferences, and graphics selections are safely cross-synchronized across all launcher and runtime profiles.

---

## 🖥️ System Requirements Specification

| Component | Minimum Specification (720p / 30–60 FPS) | Recommended Specification (1080p / 60 FPS Locked) | Ultra 4K Enthusiast (4K / 60–120+ FPS) |
| :--- | :--- | :--- | :--- |
| **Operating System** | Windows 10 (64-bit) Build 1909+ | Windows 10 / 11 (64-bit) Latest | Windows 11 (64-bit) Latest |
| **Processor (CPU)** | Intel Core i3-2100 / AMD FX-6300 (4 threads) | Intel Core i5-8400 / AMD Ryzen 5 2600 | Intel Core i7-12700K / AMD Ryzen 7 7800X3D |
| **System Memory (RAM)** | 4 GB to 6 GB | 8 GB to 16 GB Dual-Channel | 16 GB to 32 GB DDR4 / DDR5 |
| **Graphics (NVIDIA)** | GeForce GTX 650 / GTX 750 (1 GB) | GeForce GTX 1060 (6 GB) / RTX 3050 | GeForce RTX 3080 / RTX 4070 / RTX 5050 Mobile |
| **Graphics (AMD)** | Radeon HD 7750 / Radeon 760M (iGPU) | Radeon RX 580 (8 GB) / RX 6600 | Radeon RX 6800 XT / RX 7800 XT |
| **Graphics API** | DirectX 9.0c / D3D 11.1 / Vulkan 1.3 | DirectX 11 / DirectX 12 / Vulkan 1.3 | DirectX 11 / DirectX 12 / Vulkan 1.3 |
| **Dedicated VRAM** | **820 MB** (Enforced Hard Budget) | **1720 MB – 2048 MB** | **4096 MB – 8192 MB+** |
| **Storage Drive** | 45 GB (Standard SATA HDD) | 45 GB (Solid State Drive - SSD) | 45 GB (NVMe PCIe SSD) |

---

## 🔬 Architectural & Engineering Overview (Technical 30%)

The technical foundation of the *Sic Parvis Magna* architecture is built upon mathematical determinism, cache-line locality, zero-allocation game loops, and strict hardware budget enforcement.

```mermaid
flowchart TD
    subgraph Host ["PC Runtime & Presentation Layer"]
        Launcher["SPM WPF / .NET 10 Launcher"]
        Overlay["In-Game ImGui 1.91 Overlay"]
        Pacer["Microsecond Frame Pacer (QPC / Waitable)"]
    end

    subgraph Core ["SPM Graphics Core Engine"]
        CmdParser["PS4 Command Stream Interpreter"]
        Arena["Linear Arena Allocator O(1) Reset"]
        VramMgr["Deterministic VRAM Budget Manager (820 MB Cap)"]
        RingBuf["Dynamic Ring Buffer (MAP_WRITE_NO_OVERWRITE)"]
        RTPool["Transient Render Target Aliasing Pool"]
    end

    subgraph Backends ["Hardware Graphics Backends"]
        D3D9["Direct3D 9Ex (FlipEx SM3.0)"]
        D3D11["Direct3D 11.1 (Flip-Discard)"]
        D3D12["Direct3D 12 (Direct Command Queue)"]
        VK["Vulkan 1.3 (Native WSI Swapchain)"]
    end

    Launcher --> Core
    Overlay --> Core
    Pacer --> Core
    Core --> Backends
    CmdParser --> Arena
    Arena --> VramMgr
    VramMgr --> RTPool
```

### 1. Deterministic VRAM Budgeting & Dual-Accounting
Unlike conventional ports that suffer from unbounded VRAM leaks or controller thrashing, *Sic Parvis Magna* enforces a centralized [`VramBudgetManager`](Project/include/unc/graphics/vram_budget_mgr.hpp):
* **Motor vs Driver Telemetry:** Telemetry distinctly measures engine-managed resources (textures, buffers, dynamic render targets) separately from OS driver allocations (DWM, swapchain buffers).
* **Strict 820 MiB Bound:** Under the 820 MB profile, the engine limits internal allocations to $\le 859,832,320\text{ bytes}$, triggering predictive eviction of distant Mip 0 levels before PCIe overcommit occurs.
* **Low-Texture Mip-0 Skipping:** Setting `texture_quality=low` establishes a global LOD bias of $+1.0$, allowing the runtime to load textures directly from Mip 1, reducing texture memory footprints by $75\%$ without host decompression spikes.

### 2. Transient Render Target Aliasing Pool
Graphics memory for intermediate passes is multiplexed through a memory-mapped physical pool ([`TransientRenderTargetPool`](Project/include/unc/graphics/transient_rt_pool.hpp)), recycling memory across sequential phases:
$$\text{Memory Footprint}_{\text{Aliased}} = \max(\text{Size}_{\text{Shadows}}, \text{Size}_{\text{Lighting}}, \text{Size}_{\text{PostProcess}}) \ll \sum \text{Sizes}$$
* **Atlas Pass:** Sun and spotlight shadow buffers occupy Physical Block 0 ($16\text{ MB}$).
* **G-Buffer Pass:** Albedo, normals, and depth are written to Physical Block 1 ($64\text{ MB}$).
* **SSAO / Post-Processing:** Reuses Block 0 at half-resolution ($\frac{W}{2} \times \frac{H}{2}$) for ambient occlusion, bloom, and tone mapping, saving up to $400\text{ MB}$ of dedicated video memory.

### 3. Microsecond Frame Pacing & Stability Invariants
Frame delivery is governed by high-precision QueryPerformanceCounter (QPC) timing coupled with Windows multimedia timers (`timeBeginPeriod(1)`) ([`FramePacer`](Project/include/unc/core/frame_pacer.hpp)):
$$\Delta T_{99\%} = |T_i - T_{i-1}| < 0.45\text{ ms}, \qquad T_{\text{target}} = 16.666\text{ ms (60 FPS)}$$
* Eliminates micro-stuttering and irregular queue latency.
* Decouples the guest emulation tick from presentation wait intervals.
* Zero dynamic memory allocations in the critical hot path: all frame-transient descriptors and matrices allocate from [`LinearArenaAllocator`](Project/include/unc/core/arena_allocator.hpp) aligned to $64\text{-byte}$ cache boundaries (`alignas(64)`).

---

## 🚀 Quick Start Guide

### Option 1: Direct Launcher (Recommended)
1. Run **`Uncharted - Sic Parvis Magna Collection.exe`** from the root folder.
2. Verify or select your game data path (`CUSA02344`).
3. Select your active GPU from the top-right dropdown (**NVIDIA GeForce RTX** or **AMD Radeon / Integrated**).
4. Click **iGPU**, **MEDIO 2GB**, or **ALTO** to auto-configure settings, or fine-tune them in the **Gráficos y VRAM** tab.
5. Click **► JUGAR** to start your adventure!

### Option 2: Command Line & Developer Tools
```powershell
# Execute Drake's Fortune (Part 1) with native Direct3D 9Ex and 820 MB budget
python -u Project\tools\probe_uncharted.py --boot --seconds 0 --executable "eboot.bin"

# Run the automated Invariants and VRAM audit test suite
.\Build\Windows\spm-invariants-test.exe

# Run the Graphics Settings & Keybind Persistence test
.\Build\Windows\graphics-settings-test.exe

# Package clean distribution binaries and update SHA-256 manifests
powershell -ExecutionPolicy Bypass -File Project\tools\Package.ps1
```

---

## ⌨️ Controls & Keymapping Reference

| Action | Keyboard / Mouse | PlayStation Controller | Xbox Controller |
| :--- | :--- | :--- | :--- |
| **Move Drake** | `W` / `A` / `S` / `D` | Left Stick | Left Stick |
| **Aim Camera** | Mouse Movement | Right Stick | Right Stick |
| **Fire Weapon** | Left Mouse Button | `R2` Trigger | Right Trigger (`RT`) |
| **Aim Weapon** | Right Mouse Button | `L2` Trigger | Left Trigger (`LT`) |
| **Jump / Climb** | `Space` | `✕` Cross | `A` Button |
| **Cover / Roll / Drop** | `Left Ctrl` | `○` Circle | `B` Button |
| **Melee Combat** | `F` | `□` Square | `X` Button |
| **Interact / Pick Up** | `R` | `△` Triangle | `Y` Button |
| **Reload Weapon** | `E` | `R1` Shoulder | Right Bumper (`RB`) |
| **Throw Grenade** | `Q` | `L1` Shoulder | Left Bumper (`LB`) |
| **Sprint / Left Stick Press** | `Left Shift` | `L3` (Stick Click) | `LS` (Stick Click) |
| **Zoom Scope / Right Stick** | `V` / Middle Mouse | `R3` (Stick Click) | `RS` (Stick Click) |
| **Drake's Journal** | `Tab` / `Backspace` | Touchpad Click | `View` / `Back` |
| **Pause / Options Menu** | `Escape` / `Enter` | `Options` Button | `Menu` / `Start` |
| **In-Game Settings Overlay** | `Insert` | `L2` + `R3` (Hold) | `LT` + `RS` (Hold) |

---

## 📜 Legal & Community Attribution

* **UNCHARTED** is a registered trademark of **Sony Interactive Entertainment LLC** and **Naughty Dog LLC**.
* This project is an independent non-commercial preservation and reverse engineering effort for legitimate owners of *Uncharted: The Nathan Drake Collection* (PS4).
* No proprietary game assets, compressed audio banks, or copyrighted binaries are distributed within this repository.
* Licensed under the [GNU General Public License v2.0 or later (GPL-2.0-or-later)](LICENSE).
