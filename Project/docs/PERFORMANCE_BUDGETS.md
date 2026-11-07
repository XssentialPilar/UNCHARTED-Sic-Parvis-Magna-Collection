# UNCHARTED: Sic Parvis Magna Collection — Performance & VRAM Budgets (2015 Base Specification)

## 1. Hardware Targets & Acceptance Metrics

| Tier | Target CPU | System RAM | Target GPU | Physical VRAM | Target Resolution & Framerate |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Minimum (2015 Entry)** | Intel Core 2 Quad Q8400 @ 2.66 GHz / Core i3-2100 / AMD Phenom II X4 945 @ 3.0 GHz | $4\text{ GB}$ | GeForce GTX 460 / GTX 550 Ti / GTX 650 / GTX 750 / Radeon HD 5770 / HD 6790 / HD 7750 | $1\text{ GB}$ | $1280 \times 720$ @ $30\text{--}60\text{ FPS}$ |
| **Recommended (2015 Mid-Range)** | Intel Core i5-2400 @ 3.1 GHz / AMD FX-6300 @ 3.5 GHz | $6\text{--}8\text{ GB}$ | GeForce GTX 750 Ti / GTX 660 / Radeon HD 7850 / R7 265 / R9 270 | $2\text{ GB}$ | $1920 \times 1080$ @ $60\text{ FPS}$ locked |

### 1.1 Hard VRAM Caps & Non-Negotiable Telemetry Invariants
1. **1 GB VRAM Hard Cap:** Total device-local usage $\le 820\text{ MB}$ (`859,832,320` bytes). Zero PCIe shared-memory paging (`NonLocalUsage == 0`).
2. **2 GB VRAM Hard Cap:** Total device-local usage $\le 1720\text{ MB}$ (`1,803,550,720` bytes) at $1080\text{p}$ PS4 settings, reserving $\ge 328\text{ MB}$ for Windows DWM and DXGI swapchain overhead.
3. **60 FPS Frametime Invariant:**
   $$T_{\text{frame}} = \frac{1000\text{ ms}}{60} = 16.666\text{ ms}$$
4. **Maximum Frame-to-Frame Jitter (99th Percentile):**
   $$\Delta T_{99\%} = |T_i - T_{i-1}| < 0.45\text{ ms}$$
5. **Subsystem Frame Budget ($16.66\text{ ms}$ Total):**
   - Input Normalization & UI Transforms: $\le 1.20\text{ ms}$
   - Culling, Command Sorting & CPU Render Submission: $\le 4.50\text{ ms}$
   - GPU Execution (Shadows + Geometry + Lighting + Post): $\le 9.50\text{ ms}$
   - Safety Margin & Present Synchronization: $\ge 1.46\text{ ms}$

---

## 2. Four-Barrier VRAM Engineering

### 2.1 Real-Time DXGI Telemetry (`VramBudgetManager`)
Before processing the resource upload queue each frame, `IDXGIAdapter3::QueryVideoMemoryInfo` (`DXGI_MEMORY_SEGMENT_GROUP_LOCAL`) is queried:

$$\text{UsageRatio} = \frac{\text{CurrentUsage}}{\text{Budget}}$$

- **Normal ($\text{UsageRatio} < 0.80$):** Full Mip 0 and Mip 1 streaming enabled (`RecommendedMinMipLevel = 0`).
- **Elevated / Yellow Alert ($0.80 \le \text{UsageRatio} < 0.88$):** New Mip 0 promotions are frozen for secondary textures (`RecommendedMinMipLevel = 1`).
- **Critical / Red Alert ($\text{UsageRatio} \ge 0.88$):** Immediate LRU eviction of Mip 0 surfaces outside $5\text{ m}$ camera radius + `ID3D11DeviceContext1::DiscardResource` on transient buffers (`RecommendedMinMipLevel = 1` on 2 GB, `2` under extreme pressure).

### 2.2 Mip-Tail Pinning & Mip 0 Drop
$$\text{Bytes}(\text{Mip}_{k+1}) = \frac{1}{4} \times \text{Bytes}(\text{Mip}_k)$$
- **Pinned Mip-Tail ($\text{Mip}_2 \dots \text{Mip}_N$):** $\le 512 \times 512$ levels remain permanently resident ($< 80\text{ MB}$ total).
- **1 GB GPU Bias:** `MostDetailedMip = +1` reduces texture memory footprint by $75\%$.

### 2.3 Mandatory Hardware Block Compression (BC1–BC7)

| Map Type | Mandatory DXGI Format | Bits/Pixel | Size ($2048 \times 2048$ No-Mip) | Implementation Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Opaque Albedo / Diffuse** | `DXGI_FORMAT_BC1_UNORM_SRGB` | $4\text{ bpp}$ | $2.00\text{ MB}$ | $8:1$ compression vs RGBA8 ($16\text{ MB}$) |
| **Roughness / AO Masks** | `DXGI_FORMAT_BC4_UNORM` | $4\text{ bpp}$ | $2.00\text{ MB}$ | Single 8-bit high-precision channel |
| **Normal Maps** | `DXGI_FORMAT_BC5_UNORM` | $8\text{ bpp}$ | $4.00\text{ MB}$ | Stores $X, Y$; reconstruct $Z$ in Pixel Shader |
| **Alpha Albedo / HD UI** | `DXGI_FORMAT_BC3_UNORM_SRGB` / `BC7_UNORM_SRGB` | $8\text{ bpp}$ | $4.00\text{ MB}$ | BC7 on FL `11_0` GPUs, BC3 fallback |

$$\text{BC5 Normal Reconstruction:}\quad N_{xy} = \text{Sample}_{xy} \times 2.0 - 1.0, \quad N_z = \sqrt{\max\left(0.0, 1.0 - (N_x^2 + N_y^2)\right)}$$

### 2.4 Transient Render Target Aliasing (`TransientRenderTargetPool`)
- **Phase 1 (Shadows):** Shadow Atlas $2048 \times 2048$ ($16\text{ MB}$)
- **Phase 2 (Geometry):** G-Buffer / Scene Color HDR + Depth ($64\text{ MB}$)
- **Phase 3 (Lighting):** Reuses Transient Surface A ($16\text{ MB}$) for Half-Res SSAO ($\frac{W}{2} \times \frac{H}{2}$)
- **Phase 4 (Post-Processing):** Ping-pongs Transient Surfaces A & B for Half-Res Bloom, Motion Blur, and Tonemapping
