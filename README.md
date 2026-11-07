# UNCHARTED: Sic Parvis Magna Collection

![Uncharted: Sic Parvis Magna Collection Architecture](Project/docs/diagram.png)

> **"Sic Parvis Magna"** — *Greatness from small beginnings.*
>
> Runtime layer, display adaptation, low-level reverse engineering, and deterministic performance framework for PC.

---

## 1. Directrices de Desarrollo y Estándares de Ingeniería C++20/C++23

* **Tolerancia Cero a APIs Alucinadas:** Todas las llamadas invocan exclusivamente cabeceras verificadas en [`Project/include/unc/`](Project/include/unc/), [`Project/vendor/`](Project/vendor/) y el Windows SDK (`d3d11_1.h`, `dxgi1_4.h`, `windows.h`).
* **Prohibición de Asignaciones Dinámicas en el Hot Path:** Toda memoria temporal por fotograma se sirve desde [`unc::core::LinearArenaAllocator`](Project/include/unc/core/arena_allocator.hpp), que se reinicia en $O(1)$ al finalizar cada `Present()` sin llamadas al sistema operativo.
* **Diseño Orientado a Datos y Alineación de Caché (`alignas(64)`):** Las estructuras críticas entre hilos ([`LinearArenaAllocator`](Project/include/unc/core/arena_allocator.hpp), [`FramePacer`](Project/include/unc/core/frame_pacer.hpp), [`SpscQueue`](Project/include/unc/core/spsc_queue.hpp), [`VramBudgetManager`](Project/include/unc/graphics/vram_budget_mgr.hpp), [`DynamicRingBuffer`](Project/include/unc/graphics/dynamic_ring_buffer.hpp), [`TransientRenderTargetPool`](Project/include/unc/graphics/transient_rt_pool.hpp), [`InputMapper`](Project/include/unc/input/input_mapper.hpp)) están alineadas a líneas de caché de $64\text{ bytes}$ para eliminar *false sharing*.
* **Validación Determinista:** Todo retorno `HRESULT` se valida con `UNC_THROW_IF_FAILED(hr)` / `THROW_IF_FAILED(hr)` ([`Project/include/unc/core.hpp`](Project/include/unc/core.hpp)).

---

## 2. Presupuesto de Hardware y Objetivos de Telemetría (Especificación Base 2015)

| Componente | Requisitos Mínimos (720p / 30–60 FPS, Calidad Media-Baja) | Requisitos Recomendados (1080p / 60 FPS Fijos, Ajustes PS4) |
| :--- | :--- | :--- |
| **Sistema Operativo** | Windows 7 SP1 / 8.1 (64-bit) | Windows 7 SP1 / 8.1 / 10 (64-bit) |
| **Procesador (CPU)** | Intel Core 2 Quad Q8400 @ 2.66 GHz / AMD Phenom II X4 945 @ 3.0 GHz (o Intel Core i3-2100) | Intel Core i5-2400 @ 3.1 GHz / AMD FX-6300 @ 3.5 GHz |
| **Memoria RAM** | $4\text{ GB}$ (`GOW3_LOW_RAM=1`) | $6\text{ GB}$ a $8\text{ GB}$ |
| **Tarjeta Gráfica (NVIDIA)** | GeForce GTX 460 ($1\text{ GB}$) / GTX 550 Ti / GTX 650 / GTX 750 ($1\text{ GB}$) | GeForce GTX 750 Ti ($2\text{ GB}$) / GeForce GTX 660 ($2\text{ GB}$) |
| **Tarjeta Gráfica (AMD)** | Radeon HD 5770 ($1\text{ GB}$) / Radeon HD 6790 / HD 7750 ($1\text{ GB}$) | Radeon HD 7850 ($2\text{ GB}$) / Radeon R7 265 / R9 270 ($2\text{ GB}$) |
| **DirectX / Gráficos** | DirectX 11.1 (Feature Level `11_0`) + DXGI 1.4 / Vulkan 1.3 | DirectX 11.1 (Feature Level `11_0`) + DXGI 1.4 / Vulkan 1.3 |
| **VRAM Física y Límite Duro** | $1\text{ GB}$ (Cap duro $\le 820\text{ MB}$ / `859,832,320` B, $0$ PCIe paging) | $2\text{ GB}$ (Cap duro $\le 1720\text{ MB}$ / `1,803,550,720` B, $\ge 328\text{ MB}$ DWM) |
| **Almacenamiento** | $\sim 40\text{ GB}$ (HDD estándar) | $\sim 40\text{ GB}$ (HDD 7200 RPM o SSD) |

### 2.1. Métricas de Frametime Innegociables
* **Invariante de Frametime a $60\text{ FPS}$:**
  $$T_{\text{frame}} = \frac{1000\text{ ms}}{60} = 16.666\text{ ms}$$
* **Varianza Máxima Permitida (Frame-to-Frame Jitter):**
  $$\Delta T_{99\%} = |T_i - T_{i-1}| < 0.45\text{ ms}$$
* **Presupuesto por Subsistema ($16.66\text{ ms}$):**
  * Lógica y Transformaciones de Entrada/UI: $\le 1.20\text{ ms}$
  * Culling, Ordenamiento de Comandos y Envío (CPU Render Thread): $\le 4.50\text{ ms}$
  * Ejecución GPU (Sombras + Geometría + Iluminación + Postprocesado): $\le 9.50\text{ ms}$
  * Margen de Seguridad y Sincronización de Presentación: $\ge 1.46\text{ ms}$

---

## 3. Ingeniería de Memoria de Vídeo (VRAM) para GPUs de $1\text{ GB}$ y $2\text{ GB}$

### 3.1. Telemetría DXGI en Tiempo Real ([`VramBudgetManager`](Project/include/unc/graphics/vram_budget_mgr.hpp))
$$\text{UsageRatio} = \frac{\text{CurrentUsage}}{\text{Budget}}$$
* **Normal ($\text{UsageRatio} < 0.80$):** Streaming de *Mip 0* y *Mip 1* habilitado.
* **Elevated ($0.80 \le \text{UsageRatio} < 0.88$):** Se congelan promociones a *Mip 0* para texturas secundarias; se fuerza *Mip 1*.
* **Critical ($\text{UsageRatio} \ge 0.88$):** Desalojo inmediato en orden LRU de niveles *Mip 0* a más de $5\text{ metros}$ de la cámara activa + `DiscardResource` en superficies transitorias.

### 3.2. Streaming de Mipmaps y Mip-Tail Pinning
$$\text{Bytes}(\text{Mip}_{k+1}) = \frac{1}{4} \times \text{Bytes}(\text{Mip}_k)$$
* Los niveles $\text{Mip}_2 \dots \text{Mip}_N$ ($\le 512 \times 512$) permanecen fijados (*pinned*) en VRAM ($< 80\text{ MB}$). En GPUs de $1\text{ GB}$, `GetRecommendedMinMipLevel()` fija `MostDetailedMip = +1`, reduciendo un $75\%$ el peso de las texturas.

### 3.3. Compresión Estricta por Bloques (BC1 a BC7) y Reconstrucción BC5 ([`dx11_backend.hpp`](Project/include/unc/graphics/dx11_backend.hpp))
$$N_{xy} = \text{Sample}_{xy} \times 2.0 - 1.0, \qquad N_z = \sqrt{\max\left(0.0, 1.0 - (N_x^2 + N_y^2)\right)}$$

### 3.4. Aliasing Temporal de Render Targets ([`TransientRenderTargetPool`](Project/include/unc/graphics/transient_rt_pool.hpp))
* **Fase 1 (Sombras):** Atlas $2048 \times 2048$ ($16\text{ MB}$, Bloque Físico 0).
* **Fase 2 (Geometría):** G-Buffer / Scene Color HDR + Depth ($64\text{ MB}$, Bloque Físico 1).
* **Fase 3 (Iluminación):** Reutiliza el Bloque Físico 0 ($16\text{ MB}$) a mitad de resolución ($\frac{W}{2} \times \frac{H}{2}$) para SSAO.
* **Fase 4 (Postprocesado):** Ping-Pong entre Bloques Físicos 0 y 2 a mitad de resolución ($\frac{W}{2} \times \frac{H}{2}$) para Bloom, Motion Blur y Tonemapping.

---

## 4. Invariantes Matemáticos de Pantalla, Entrada y Presentación

* **Desacoplamiento de Swapchain y Viewport:**
  $$\text{Aspect Ratio} = \frac{\text{Width}_{\text{viewport}}}{\text{Height}_{\text{viewport}}}$$
* **Normalización de Coordenadas de Entrada con Ejes Independientes ([`InputMapper`](Project/include/unc/input/input_mapper.hpp)):**
  $$X_{\text{game}} = \left( X_{\text{client}} - \text{Viewport}_{X} \right) \times \left( \frac{\text{Width}_{\text{internal}}}{\text{Viewport}_{\text{width}}} \right), \qquad Y_{\text{game}} = \left( Y_{\text{client}} - \text{Viewport}_{Y} \right) \times \left( \frac{\text{Height}_{\text{internal}}}{\text{Viewport}_{\text{height}}} \right)$$
* **Orden Estricto de 7 Pasos en `IDXGISwapChain::Present` ([`ExecuteDeterministic7StepPresent`](Project/native/graphics/swapchain_hook.cpp)):**
  1. Iniciar cronómetro QPC (`FramePacer::BeginFrame`).
  2. Consultar telemetría de VRAM (`VramBudgetManager::PollAndEnforceBudget`) y desalojar Mip 0 LRU si $\text{UsageRatio} \ge 0.88$.
  3. Procesar hasta $2.5\text{ MB}$ de la cola `SpscQueue` de subida asíncrona.
  4. Evaluar *dirty flags* y actualizar constantes de proyección vía `DynamicRingBuffer` (`MAP_WRITE_NO_OVERWRITE`).
  5. Renderizar capas de UI/HUD adaptadas.
  6. Reiniciar el `LinearArenaAllocator` del fotograma actual en $O(1)$.
  7. Ejecutar `FramePacer::WaitForTargetFrameTime` y llamar a `oPresent(SyncInterval, Flags)`.

---

## 5. Compilación, Empaquetado y Ejecución

```powershell
powershell -ExecutionPolicy Bypass -File .\Project\tools\Build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File .\Project\tools\Package.ps1
```
