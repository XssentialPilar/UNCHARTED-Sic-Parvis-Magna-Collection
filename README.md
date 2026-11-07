# UNCHARTED: Sic Parvis Magna Collection



> **"Sic Parvis Magna"** — *Greatness from small beginnings.*
>
> Runtime layer, display adaptation, low-level reverse engineering, and deterministic performance framework for PC.

---

## 1. Development Directives and C++20/C++23 Engineering Standards

- **Zero Tolerance for Hallucinated APIs:** All invocations exclusively reference verified headers in [`Project/include/unc/`](Project/include/unc/), [`Project/vendor/`](Project/vendor/), and the Windows SDK (`d3d11_1.h`, `dxgi1_4.h`, `windows.h`).
- **Ban on Dynamic Allocations in the Hot Path:** All per-frame transient memory is served from [`unc::core::LinearArenaAllocator`](Project/include/unc/core/arena_allocator.hpp), which resets in $O(1)$ upon completion of each `Present()` with zero OS-level calls.
- **Data-Oriented Design and Cache Alignment (`alignas(64)`):** Critical inter-thread structures ([`LinearArenaAllocator`](Project/include/unc/core/arena_allocator.hpp), [`FramePacer`](Project/include/unc/core/frame_pacer.hpp), [`SpscQueue`](Project/include/unc/core/spsc_queue.hpp), [`VramBudgetManager`](Project/include/unc/graphics/vram_budget_mgr.hpp), [`DynamicRingBuffer`](Project/include/unc/graphics/dynamic_ring_buffer.hpp), [`TransientRenderTargetPool`](Project/include/unc/graphics/transient_rt_pool.hpp), [`InputMapper`](Project/include/unc/input/input_mapper.hpp)) are aligned to $64\text{-byte}$ cache lines to eliminate false sharing.
- **Deterministic Validation:** Every `HRESULT` return value is validated via `UNC_THROW_IF_FAILED(hr)` / `THROW_IF_FAILED(hr)` ([`Project/include/unc/core.hpp`](Project/include/unc/core.hpp)).

---

## 2. Hardware Budget and Telemetry Targets (2015 Baseline Specification)

| Component | Minimum Requirements (720p / 30–60 FPS, Low-Medium Quality) | Recommended Requirements (1080p / Locked 60 FPS, PS4 Settings) |
| :--- | :--- | :--- |
| **Operating System** | Windows 10 / 11 (64-bit) | Windows 10 / 11 (64-bit) |
| **Processor (CPU)** | Intel Core 2 Quad Q8400 @ 2.66 GHz / AMD Phenom II X4 945 @ 3.0 GHz (or Intel Core i3-2100) | Intel Core i5-2400 @ 3.1 GHz / AMD FX-6300 @ 3.5 GHz |
| **System RAM** | $4\text{ GB}$ (`GOW3_LOW_RAM=1`) | $6\text{ GB}$ to $8\text{ GB}$ |
| **Graphics Card (NVIDIA)** | GeForce GTX 460 ($1\text{ GB}$) / GTX 550 Ti / GTX 650 / GTX 750 ($1\text{ GB}$) | GeForce GTX 750 Ti ($2\text{ GB}$) / GeForce GTX 660 ($2\text{ GB}$) |
| **Graphics Card (AMD)** | Radeon HD 5770 ($1\text{ GB}$) / Radeon HD 6790 / HD 7750 ($1\text{ GB}$) | Radeon HD 7850 ($2\text{ GB}$) / Radeon R7 265 / R9 270 ($2\text{ GB}$) |
| **DirectX / Graphics** | DirectX 11.1 (Feature Level `11_0`) + DXGI 1.4 / Vulkan 1.3 | DirectX 11.1 (Feature Level `11_0`) + DXGI 1.4 / Vulkan 1.3 |
| **Physical VRAM & Hard Cap** | $1\text{ GB}$ (Hard cap $\le 820\text{ MB}$ / `859,832,320` B, $0$ PCIe paging) | $2\text{ GB}$ (Hard cap $\le 1720\text{ MB}$ / `1,803,550,720` B, $\ge 328\text{ MB}$ DWM) |
| **Storage** | $\sim 40\text{ GB}$ (Standard HDD) | $\sim 40\text{ GB}$ (7200 RPM HDD or SSD) |

### 2.1. Non-Negotiable Frametime Metrics
- **Locked 60 FPS Frametime Invariant:**
  $$T_{\text{frame}} = \frac{1000\text{ ms}}{60} = 16.666\text{ ms}$$
- **Maximum Allowable Variance (Frame-to-Frame Jitter):**
  $$\Delta T_{99\%} = |T_i - T_{i-1}| < 0.45\text{ ms}$$
- **Per-Subsystem Budget ($16.66\text{ ms}$):**
  * Input Logic and UI Transformations: $\le 1.20\text{ ms}$
  * Culling, Command Sorting, and Submission (CPU Render Thread): $\le 4.50\text{ ms}$
  * GPU Execution (Shadows + Geometry + Lighting + Post-Processing): $\le 9.50\text{ ms}$
  * Safety Margin and Presentation Synchronization: $\ge 1.46\text{ ms}$

---

## 3. Video Memory (VRAM) Engineering for 1 GB and 2 GB GPUs

### 3.1. Real-Time DXGI Telemetry ([`VramBudgetManager`](Project/include/unc/graphics/vram_budget_mgr.hpp))
$$\text{UsageRatio} = \frac{\text{CurrentUsage}}{\text{Budget}}$$
- **Normal ($\text{UsageRatio} < 0.80$):** *Mip 0* and *Mip 1* streaming enabled.
- **Elevated ($0.80 \le \text{UsageRatio} < 0.88$):** Promotions to *Mip 0* frozen for secondary textures; *Mip 1* enforced.
- **Critical ($\text{UsageRatio} \ge 0.88$):** Immediate LRU-order eviction of *Mip 0* levels beyond $5\text{ meters}$ from active camera + `DiscardResource` on transient surfaces.

### 3.2. Mipmap Streaming and Mip-Tail Pinning
$$\text{Bytes}(\text{Mip}_{k+1}) = \frac{1}{4} \times \text{Bytes}(\text{Mip}_k)$$
- Levels $\text{Mip}_2 \dots \text{Mip}_N$ ($\le 512 \times 512$) remain pinned in VRAM ($< 80\text{ MB}$). On $1\text{ GB}$ GPUs, `GetRecommendedMinMipLevel()` clamps `MostDetailedMip = +1`, reducing texture memory footprint by $75\%$.

### 3.3. Strict Block Compression (BC1 to BC7) and BC5 Reconstruction ([`dx11_backend.hpp`](Project/include/unc/graphics/dx11_backend.hpp))
$$N_{xy} = \text{Sample}_{xy} \times 2.0 - 1.0, \qquad N_z = \sqrt{\max\left(0.0, 1.0 - (N_x^2 + N_y^2)\right)}$$

### 3.4. Transient Render Target Aliasing ([`TransientRenderTargetPool`](Project/include/unc/graphics/transient_rt_pool.hpp))
- **Phase 1 (Shadows):** Atlas $2048 \times 2048$ ($16\text{ MB}$, Physical Block 0).
- **Phase 2 (Geometry):** G-Buffer / Scene Color HDR + Depth ($64\text{ MB}$, Physical Block 1).
- **Phase 3 (Lighting):** Reuses Physical Block 0 ($16\text{ MB}$) at half resolution ($\frac{W}{2} \times \frac{H}{2}$) for SSAO.
- **Phase 4 (Post-Processing):** Ping-pong between Physical Blocks 0 and 2 at half resolution ($\frac{W}{2} \times \frac{H}{2}$) for Bloom, Motion Blur, and Tonemapping.

---

## 4. Mathematical Invariants for Display, Input, and Presentation

- **Swapchain and Viewport Decoupling:**
  $$\text{Aspect Ratio} = \frac{\text{Width}_{\text{viewport}}}{\text{Height}_{\text{viewport}}}$$
- **Independent-Axis Input Coordinate Normalization ([`InputMapper`](Project/include/unc/input/input_mapper.hpp)):**
  $$X_{\text{game}} = \left( X_{\text{client}} - \text{Viewport}_{X} \right) \times \left( \frac{\text{Width}_{\text{internal}}}{\text{Viewport}_{\text{width}}} \right), \qquad Y_{\text{game}} = \left( Y_{\text{client}} - \text{Viewport}_{Y} \right) \times \left( \frac{\text{Height}_{\text{internal}}}{\text{Viewport}_{\text{height}}} \right)$$
- **Strict 7-Step Sequence in `IDXGISwapChain::Present` ([`ExecuteDeterministic7StepPresent`](Project/native/graphics/swapchain_hook.cpp)):**
  1. Start QPC timer (`FramePacer::BeginFrame`).
  2. Poll VRAM telemetry (`VramBudgetManager::PollAndEnforceBudget`) and evict LRU Mip 0 if $\text{UsageRatio} \ge 0.88$.
  3. Dispatch up to $2.5\text{ MB}$ from the asynchronous upload `SpscQueue`.
  4. Evaluate dirty flags and update projection constants via `DynamicRingBuffer` (`MAP_WRITE_NO_OVERWRITE`).
  5. Render adapted UI/HUD layers.
  6. Reset the current frame's `LinearArenaAllocator` in $O(1)$.
  7. Execute `FramePacer::WaitForTargetFrameTime` and invoke `oPresent(SyncInterval, Flags)`.

---

## 5. Build, Packaging, and Execution

```powershell
powershell -ExecutionPolicy Bypass -File .\Project\tools\Build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File .\Project\tools\Package.ps1
```
