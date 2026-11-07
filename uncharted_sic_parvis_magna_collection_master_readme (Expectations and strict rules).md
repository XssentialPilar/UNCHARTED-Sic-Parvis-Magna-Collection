# UNCHARTED: Sic Parvis Magna Collection

> Runtime layer, display adaptation, low-level reverse engineering, and deterministic performance framework for PC.

---

## 1. Directrices de Desarrollo para Agentes de IA (Antigravity / Argon / Codex)

Este repositorio está diseñado como un entorno autocontenido para ingeniería de sistemas asistida por agentes. Cualquier modelo que opere sobre este código debe cumplir de forma estricta e innegociable las siguientes reglas:

### 1.1. Filosofía de Implementación y Cero Alucinaciones

* **Rol Técnico:** Actúas como un ingeniero principal de motores gráficos y sistemas en C++ de bajo nivel, especializado en DirectX 11, DirectX 12, Vulkan, programación de sistemas Win32 y optimización de *frametime* en hardware restringido.
* **Tolerancia Cero a APIs Alucinadas:** Está estrictamente prohibido invocar funciones, métodos, macros o estructuras de bibliotecas externas que no estén declaradas explícitamente en `vendor/`, `include/` o el Windows SDK. Antes de escribir cualquier llamada externa, inspecciona la cabecera real.
* **Entrega Completa de Archivos:** Al crear o modificar un archivo, entrega siempre el código completo de principio a fin. Nunca trunques código ni uses comentarios evasivos como `// ... resto de la implementación ...`.
* **Resolución Determinista de Errores de Compilación:** Ante fallos de MSBuild o CMake, analiza el código exacto del compilador (`E0020`, `C2039`, `C2664`, `LNK2019`), verifica la firma en la cabecera correspondiente y corrige la causa raíz sin degradar la arquitectura.

### 1.2. Estándares de Código C++ de Alto Rendimiento

* **Estándar:** C++20 / C++23 estricto.
* **Prohibición de Asignaciones Dinámicas en el Hot Path:** Está terminantemente prohibido usar `new`, `malloc`, `std::make_shared`, `std::vector::push_back` (sin `reserve` previo) o `std::function` con captura pesada dentro del bucle por fotograma (`Present`, `Update`, `RenderPass`). Toda memoria por fotograma debe servirse desde un `LinearArenaAllocator` preasignado que se reinicia en $O(1)$ al inicio del fotograma.
* **Diseño Orientado a Datos (Cache Locality):** Priorizar estructuras contiguas y arreglos de estructuras planas (SoA / *Structure of Arrays*) sobre árboles de punteros polimórficos. Evitar llamadas virtuales (`vtable` dispatch) en bucles que iteren más de $100$ veces por fotograma.
* **Alineación de Caché:** Las estructuras compartidas entre hilos deben alinearse a límites de línea de caché mediante `alignas(64)` para impedir *false sharing* entre núcleos de CPU.
* **Validación de APIs:** Validar todo retorno `HRESULT` mediante la macro `UNC_THROW_IF_FAILED(hr)` o manejo explícito libre de excepciones en rutas críticas.

---

## 2. Presupuesto de Hardware y Objetivos de Telemetría (Especificación Base 2015)

El motor y su capa de adaptación tienen prohibido depender de fuerza bruta o exigir tarjetas modernas de $4\text{ GB}$ o más de VRAM. El código debe garantizar fluidez absoluta en el hardware accesible de la época:

| Nivel | CPU Objetivo | RAM Sistema | GPU Objetivo | VRAM Física | Resolución / Meta |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Mínimo** | Intel Core 2 Quad Q8400 / Core i3-2100 / Phenom II X4 945 | $4\text{ GB}$ | GTX 460 / GTX 750 / HD 5770 / HD 7750 | $1\text{ GB}$ | $1280 \times 720$ @ $30\text{--}60\text{ FPS}$ |
| **Recomendado** | Intel Core i5-2400 / AMD FX-6300 | $6\text{--}8\text{ GB}$ | GTX 750 Ti / GTX 660 / HD 7850 / R9 270 | $2\text{ GB}$ | $1920 \times 1080$ @ $60\text{ FPS}$ fijos |

### 2.1. Métricas de Aceptación Innegociables

1. **Presupuesto Duro de VRAM (Modo $1\text{ GB}$):** Consumo total $\le 820\text{ MB}$. Cero paginación por bus PCIe.
2. **Presupuesto Duro de VRAM (Modo $2\text{ GB}$):** Consumo total $\le 1720\text{ MB}$ a $1080\text{p}$ con ajustes equivalentes a PS4, reservando $\ge 328\text{ MB}$ de margen libre para el compositor DWM de Windows y sobrecarga de DXGI.
3. **Invariante de Frametime a $60\text{ FPS}$:**
   $$T_{\text{frame}} = \frac{1000\text{ ms}}{60} = 16.666\text{ ms}$$
4. **Varianza Máxima Permitida (Frame-to-Frame Jitter):**
   $$\Delta T_{99\%} = |T_{i} - T_{i-1}| < 0.45\text{ ms}$$
5. **Presupuesto de Tiempo por Subsistema en un Fotograma ($16.66\text{ ms}$):**
   * Lógica y Transformaciones de Entrada/UI: $\le 1.20\text{ ms}$
   * Culling, Ordenamiento de Comandos y Envío (CPU Render Thread): $\le 4.50\text{ ms}$
   * Ejecución GPU (Sombras + Geometría + Iluminación + Postprocesado): $\le 9.50\text{ ms}$
   * Margen de Seguridad y Sincronización de Presentación: $\ge 1.46\text{ ms}$

---

## 3. Ingeniería de Memoria de Vídeo (VRAM) para GPUs de $1\text{ GB}$ y $2\text{ GB}$

El motivo principal de tirones (*stuttering*) en tarjetas de $1\text{ GB}$ y $2\text{ GB}$ es el desbordamiento del presupuesto de memoria local, lo que obliga a Windows (WDDM) a trasladar texturas a la memoria RAM del sistema a través del bus PCIe ($8\text{--}16\text{ GB/s}$ frente a los $80\text{--}150\text{ GB/s}$ de la VRAM dedicada). El agente debe implementar las siguientes cuatro barreras técnicas:

### 3.1. Telemetría DXGI en Tiempo Real y Presupuesto Adaptativo

En cada fotograma, antes de procesar la cola de carga de recursos, el subsistema gráfico debe consultar `IDXGIAdapter3::QueryVideoMemoryInfo` para el grupo `DXGI_MEMORY_SEGMENT_GROUP_LOCAL`:

$$\text{UsageRatio} = \frac{\text{CurrentUsage}}{\text{Budget}}$$

* **Estado Normal ($\text{UsageRatio} < 0.80$):** Se permite el *streaming* de niveles de detalle altos (*Mip 0* y *Mip 1*).
* **Alerta Amarilla ($0.80 \le \text{UsageRatio} < 0.88$):** Se bloquean nuevas promociones a *Mip 0* para texturas secundarias; solo se cargan *Mip 1* o inferiores.
* **Alerta Roja ($\text{UsageRatio} \ge 0.88$):** Desalojo inmediato (*eviction*) en orden LRU (*Least Recently Used*) de los niveles *Mip 0* de todas las texturas que no estén a menos de $5\text{ metros}$ de la cámara activa, seguido de `ID3D11DeviceContext1::DiscardResource` en búferes transitorios.

### 3.2. Streaming de Mipmaps con Descarte Dinámico de Mip 0

Reducir una textura un solo nivel en la cadena de *mipmaps* divide por $4$ su consumo de memoria:

$$\text{Bytes}(\text{Mip}_{k+1}) = \frac{1}{4} \times \text{Bytes}(\text{Mip}_{k})$$

* **Mip-Tail Pinning:** Los niveles pequeños ($\text{Mip}_{2}$ hasta $\text{Mip}_{N}$, es decir, $512 \times 512$ hacia abajo) se mantienen fijados (*pinned*) en un bloque estático de VRAM. Esto garantiza que jamás aparezcan mallas sin textura o parpadeos negros, consumiendo menos de $80\text{ MB}$ para todo el nivel cargado.
* **Streaming Bajo Demanda de Alta Resolución:** Los niveles $\text{Mip}_{1}$ ($1024 \times 1024$) y $\text{Mip}_{0}$ ($2048 \times 2048$) residen en un *pool* circular de tamaño fijo. En GPUs de $1\text{ GB}$, el motor fija globalmente el desplazamiento de nivel de detalle (`MipLODBias` o el `MostDetailedMip` del `D3D11_SHADER_RESOURCE_VIEW_DESC`) en $+1$, ahorrando automáticamente el $75\%$ del peso de todas las texturas del juego sin intervención del usuario.

### 3.3. Compresión Estricta por Bloques (BC1 a BC7)

Está **terminantemente prohibido** almacenar texturas de escena 3D en formatos sin comprimir (`DXGI_FORMAT_R8G8B8A8_UNORM` o `B8G8R8A8_UNORM`). Toda textura debe estar comprimida en hardware:

| Tipo de Mapa | Formato DXGI Obligatorio | Bits por Píxel | Tamaño en VRAM ($2048 \times 2048$ sin Mips) | Notas de Implementación |
| :--- | :--- | :--- | :--- | :--- |
| **Albedo Opaco / Difuso** | `DXGI_FORMAT_BC1_UNORM_SRGB` | $4\text{ bpp}$ | $2.00\text{ MB}$ | Compresión $8:1$ frente a RGBA8 ($16\text{ MB}$). |
| **Máscaras de Rugosidad / AO** | `DXGI_FORMAT_BC4_UNORM` | $4\text{ bpp}$ | $2.00\text{ MB}$ | Un solo canal de alta precisión de $8\text{ bits}$. |
| **Mapas de Normales** | `DXGI_FORMAT_BC5_UNORM` | $8\text{ bpp}$ | $4.00\text{ MB}$ | Almacena solo $X$ e $Y$. Reconstruir $Z$ en el Pixel Shader. |
| **Albedo con Alfa / UI HD** | `DXGI_FORMAT_BC3_UNORM_SRGB` / `BC7_UNORM_SRGB` | $8\text{ bpp}$ | $4.00\text{ MB}$ | Usar BC7 en GPUs Feature Level `11_0` y BC3 como respaldo. |

* **Fórmula obligatoria de reconstrucción de normales (BC5) en HLSL:**
  $$N_{xy} = \text{Sample}_{xy} \times 2.0 - 1.0$$
  $$N_{z} = \sqrt{\max\left(0.0, 1.0 - (N_x^2 + N_y^2)\right)}$$

### 3.4. Aliasing Temporal de Render Targets (Transient Resource Pool)

Los búferes de renderizado intermedios nunca deben asignarse de forma aislada y permanente. El agente debe implementar un `TransientRenderTargetPool` donde las distintas fases del fotograma reutilicen exactamente las mismas superficies físicas de memoria:

```
[Fase 1: Sombras]      -> [Atlas de Sombras 2048x2048 (16 MB)]
[Fase 2: Geometría]    -> [G-Buffer / Scene Color HDR + Depth (64 MB)]
[Fase 3: Iluminación]  -> Reutiliza superficie temporal A (16 MB) para SSAO (Half-Res 960x540)
[Fase 4: Postproceso]  -> Reutiliza superficie temporal A y B en Ping-Pong para Bloom / Motion Blur / Tonemapping
```

* **Regla de Oro de Resolución Intermedia:** Efectos de baja frecuencia espacial como **SSAO**, **Bloom** y **Depth of Field** deben calcularse a mitad de resolución ($\frac{W}{2} \times \frac{H}{2}$) con filtrado bilateral. Esto reduce a $\frac{1}{4}$ el consumo de VRAM y cuadruplica el rendimiento del ancho de banda en buses de $128\text{ bits}$ (como el de la GTX 750 Ti).

---

## 4. Arquitectura de Frametime Uniforme (Cero Micro-Stuttering)

Un promedio de $60\text{ FPS}$ no sirve si un fotograma tarda $8\text{ ms}$ y el siguiente $32\text{ ms}$. Para garantizar una línea de *frametime* completamente plana:

### 4.1. Erradicación del Stutter por Compilación de Shaders

1. **Prohibido compilar HLSL en tiempo real:** `D3DCompile` nunca debe ejecutarse durante la partida. Todos los shaders deben precompilarse fuera de línea a bytecode DXBC/DXIL mediante `fxc.exe` o `dxc.exe` durante el paso de construcción (`Build.ps1`).
2. **Precalentamiento de Estado (PSO / Shader Pre-warming):** Al cargar un nivel o iniciar el binario, el motor lee un archivo persistente `shader_cache.bin` y crea todos los objetos `ID3D11VertexShader`, `ID3D11PixelShader`, `ID3D11InputLayout` y estados de mezcla antes de mostrar el primer fotograma jugable.
3. **En DirectX 12 / Vulkan:** Compilación asíncrona en hilos secundarios usando listas de estado de pipeline empaquetadas (`ID3D12PipelineLibrary`), recurriendo a un shader *fallback* ultraligero si una variante no prioritaria aún no ha terminado.

### 4.2. Sub-asignación Mediante Ring-Buffer (Cero `CreateBuffer` en Caliente)

Llamar a `ID3D11Device::CreateBuffer`, `CreateTexture2D` o `CreateShaderResourceView` durante la partida bloquea el hilo del controlador gráfico y provoca picos de $15\text{ ms}$ a $40\text{ ms}$.

* **Implementación Obligatoria (`DynamicRingBuffer`):**
  * Al inicializar el dispositivo, se asigna un único búfer circular de $16\text{ MB}$ con `D3D11_USAGE_DYNAMIC`.
  * En cada llamada de dibujo (*draw call*), los datos de constantes y vértices dinámicos se escriben avanzando un puntero de desplazamiento (`offset`) mediante `ID3D11DeviceContext::Map` con la bandera **`D3D11_MAP_WRITE_NO_OVERWRITE`**.
  * Solo cuando el cursor alcanza el final de los $16\text{ MB}$, se envuelve al inicio ($0$) invocando una única vez **`D3D11_MAP_WRITE_DISCARD`**.

### 4.3. Frame Pacing Determinista de Precisión Sub-Milisegundo

Nunca usar `Sleep(ms)` estándar de Win32 para limitar cuadros por segundo, ya que la granularidad del planificador del kernel de Windows puede variar entre $1\text{ ms}$ y $15.6\text{ ms}$, destruyendo la cadencia de fotogramas.

* **Algoritmo Híbrido de Frame Pacing:**
  1. Configurar el *swapchain* con `DXGI_SWAP_EFFECT_FLIP_DISCARD` y `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, estableciendo `SetMaximumFrameLatency(1)` (o `2` si la GPU está al $99\%$) para eliminar la latencia de entrada.
  2. Medir el tiempo transcurrido del fotograma con `QueryPerformanceCounter` ($T_{\text{elapsed}}$).
  3. Si falta más de $1.5\text{ ms}$ para el objetivo ($16.666\text{ ms}$), esperar mediante un temporizador de alta resolución del kernel (`CreateWaitableTimerExW` con el flag `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`).
  4. Para los últimos $0.35\text{ ms}$ restantes, ejecutar un bucle de espera activa (*spin-wait*) invocando la instrucción intrínseca de CPU `_mm_pause()` hasta alcanzar el tick exacto de presentación.

### 4.4. Cuota de Subida Asíncrona por Fotograma (Time-Sliced Streaming)

Subir $50\text{ MB}$ de texturas nuevas en un solo fotograma satura el bus PCIe y congela la imagen.

* **Transferencia Fraccionada (DMA / Copy Queue):** El hilo de carga descomprime bloques en segundo plano y los deposita en una cola *lock-free* SPSC (*Single-Producer Single-Consumer*).
* **Cuota Máxima por Fotograma:** El hilo de renderizado tiene permitido subir al GPU un máximo estricto de **$2.5\text{ MB}$ por fotograma** ($\approx 150\text{ MB/s}$ a $60\text{ FPS}$). Si una textura de alta resolución requiere más espacio, su transferencia se divide por sub-recursos (*mipmaps* o *tiles*) a lo largo de varios fotogramas consecutivos sin alterar el *frametime*.

---

## 5. Invariantes Matemáticos de Pantalla y Entrada

### 5.1. Desacoplamiento de Swapchain y Viewport

* El búfer interno de renderizado (*Backbuffer*) y el área cliente de la ventana (`HWND`) son independientes.
* Jamás redimensionar el *swapchain* (`ResizeBuffers`) en cada fotograma ni dentro de `Present`. Solo redimensionar ante un evento confirmado `WM_EXITSIZEMOVE` o cambio explícito de resolución.
* La relación de aspecto efectiva se calcula como:
  $$\text{Aspect Ratio} = \frac{\text{Width}_{\text{viewport}}}{\text{Height}_{\text{viewport}}}$$
* Toda adaptación a monitores $16:10$, $21:9$ o $32:9$ aplica escalado Hor+ (*Horizontal Plus*) sobre la matriz de proyección de cámara 3D, mientras que el HUD/UI 2D se ancla a un contenedor seguro $16:9$ centrado mediante matrices ortográficas independientes por eje.

### 5.2. Normalización de Coordenadas de Entrada (Input Pipeline)

Para erradicar el desfase de puntero (*offset drift*) y el doble cursor, las coordenadas del sistema operativo se proyectan al espacio lógico del juego mediante una transformación pura sin estado acumulativo:

$$X_{\text{game}} = \left( X_{\text{client}} - \text{Viewport}_{X} \right) \times \left( \frac{\text{Width}_{\text{internal}}}{\text{Viewport}_{\text{width}}} \right)$$

$$Y_{\text{game}} = \left( Y_{\text{client}} - \text{Viewport}_{Y} \right) \times \left( \frac{\text{Height}_{\text{internal}}}{\text{Viewport}_{\text{height}}} \right)$$

* **Independencia de Ejes:** Las transformaciones sobre el eje $X$ (ej. carruseles horizontales) tienen prohibido escribir o mutar las propiedades del eje $Y$ (ej. listas verticales o inventarios con desplazamiento), evitando derivas acumulativas al abrir y cerrar menús repetidamente.

### 5.3. Orden Estricto en el Hook de `IDXGISwapChain::Present`

1. Iniciar cronómetro QPC del paso de presentación.
2. Consultar telemetría de VRAM (`QueryVideoMemoryInfo`) y ejecutar desalojos LRU si $\text{UsageRatio} \ge 0.88$.
3. Procesar un máximo de $2.5\text{ MB}$ de la cola de subida asíncrona de texturas.
4. Evaluar *dirty flags* de ventana/viewport y actualizar constantes de proyección mediante `DynamicRingBuffer` (`MAP_WRITE_NO_OVERWRITE`).
5. Renderizar capas de UI/HUD adaptadas.
6. Reiniciar el `LinearArenaAllocator` del fotograma actual.
7. Ejecutar el bloque de *Frame Pacing* híbrido y llamar al puntero original `oPresent(SyncInterval, Flags)`.

---

## 6. Arquitectura del Repositorio y Contratos de Código C++

### 6.1. Árbol de Directorios

```
UNCHARTED-Sic-Parvis-Magna-Collection/
├── .antigravity/
│   └── rules                         # Reglas de contexto para Antigravity
├── docs/
│   ├── ARCHITECTURE.md               # Diagramas de subsistemas y memoria
│   └── PERFORMANCE_BUDGETS.md        # Perfiles para GPUs de 1 GB y 2 GB
├── include/
│   └── unc/
│       ├── core/
│       │   ├── arena_allocator.hpp   # Asignador lineal O(1) sin heap en hot-path
│       │   ├── frame_pacer.hpp       # Temporizador híbrido QPC + WaitableTimer + Spin
│       │   └── spsc_queue.hpp        # Cola lock-free alineada a 64 bytes
│       ├── graphics/
│       │   ├── dx11_backend.hpp      # Dispositivo, contexto y estados precalentados
│       │   ├── dynamic_ring_buffer.hpp # Sub-asignación MAP_WRITE_NO_OVERWRITE
│       │   ├── transient_rt_pool.hpp # Aliasing de Render Targets temporales
│       │   └── vram_budget_mgr.hpp   # Telemetría DXGI, LRU y Mip0 eviction
│       └── input/
│           └── input_mapper.hpp      # Normalización matemática y soporte SDL3/GameInput
├── native/
│   ├── core/
│   │   ├── arena_allocator.cpp
│   │   └── frame_pacer.cpp
│   ├── graphics/
│   │   ├── dx11_backend.cpp
│   │   ├── dynamic_ring_buffer.cpp
│   │   ├── swapchain_hook.cpp
│   │   ├── transient_rt_pool.cpp
│   │   └── vram_budget_mgr.cpp
│   └── input/
│       └── input_mapper.cpp
├── vendor/                           # Cabeceras limpias (Headers Only)
│   ├── d3d12/
│   ├── minhook/
│   └── sdl3/
├── tools/
│   ├── Build.ps1                     # Compilación determinista CMake + MSVC
│   ├── CompileShaders.ps1            # Precompilación offline HLSL -> DXBC/DXIL
│   └── Package.ps1                   # Empaquetado y generación de hashes SHA-256
├── CMakeLists.txt
├── vcpkg.json
└── README.md
```

### 6.2. Contratos Canónicos de Interfaz (Referencia Obligatoria para el Agente)

El agente debe respetar estas firmas exactas al implementar los módulos centrales en `include/unc/`:

```cpp
// include/unc/core/arena_allocator.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>

namespace unc::core {

class alignas(64) LinearArenaAllocator {
public:
    explicit LinearArenaAllocator(std::size_t capacityBytes) noexcept;
    ~LinearArenaAllocator() noexcept;

    LinearArenaAllocator(const LinearArenaAllocator&) = delete;
    LinearArenaAllocator& operator=(const LinearArenaAllocator&) = delete;

    [[nodiscard]] void* Allocate(std::size_t sizeBytes, std::size_t alignment = 16) noexcept;

    template <typename T, typename... Args>
    [[nodiscard]] T* Emplace(Args&&... args) noexcept {
        void* mem = Allocate(sizeof(T), alignof(T));
        return mem ? new (mem) T(static_cast<Args&&>(args)...) : nullptr;
    }

    // Se invoca al final de cada Present(): costo O(1), cero llamadas al SO
    void Reset() noexcept { m_offset = 0; }

    [[nodiscard]] std::size_t GetUsedBytes() const noexcept { return m_offset; }
    [[nodiscard]] std::size_t GetCapacityBytes() const noexcept { return m_capacity; }

private:
    std::uint8_t* m_buffer{nullptr};
    std::size_t   m_capacity{0};
    std::size_t   m_offset{0};
};

} // namespace unc::core
```

```cpp
// include/unc/graphics/vram_budget_mgr.hpp
#pragma once
#include <cstdint>
#include <d3d11_1.h>
#include <dxgi1_4.h>

namespace unc::graphics {

enum class VramPressureLevel : std::uint8_t {
    Normal = 0,   // < 80% del presupuesto: Mip 0 permitido
    Elevated = 1, // 80% - 88%: Congelar promociones a Mip 0, forzar Mip 1
    Critical = 2  // >= 88%: Desalojar Mip 0 en orden LRU + DiscardResource
};

struct VramTelemetry {
    std::uint64_t budgetBytes{0};
    std::uint64_t currentUsageBytes{0};
    float         usageRatio{0.0f};
    VramPressureLevel pressure{VramPressureLevel::Normal};
};

class VramBudgetManager {
public:
    bool Initialize(IDXGIAdapter3* adapter, std::uint64_t hardCapBytes) noexcept;
    
    // Llamar una vez al inicio de cada fotograma
    VramTelemetry PollAndEnforceBudget(ID3D11DeviceContext1* ctx, std::uint64_t frameIndex) noexcept;

    // Cuota estricta de subida por fotograma (por defecto 2.5 MB)
    [[nodiscard]] bool RequestUploadSlice(std::size_t bytesToUpload) noexcept;
    void ResetFrameUploadQuota() noexcept { m_uploadedThisFrame = 0; }

    [[nodiscard]] std::uint32_t GetRecommendedMinMipLevel() const noexcept;

private:
    IDXGIAdapter3* m_adapter{nullptr};
    std::uint64_t  m_hardCapBytes{1720ULL * 1024ULL * 1024ULL}; // 1720 MB por defecto para GPUs 2 GB
    std::size_t    m_maxUploadPerFrame{2621440};                // 2.5 MB/frame
    std::size_t    m_uploadedThisFrame{0};
    VramTelemetry  m_lastTelemetry{};
};

} // namespace unc::graphics
```

```cpp
// include/unc/graphics/dynamic_ring_buffer.hpp
#pragma once
#include <cstdint>
#include <d3d11.h>

namespace unc::graphics {

struct RingAllocation {
    void*       cpuAddress{nullptr};
    std::size_t byteOffset{0};
    std::size_t sizeBytes{0};
};

class DynamicRingBuffer {
public:
    bool Initialize(ID3D11Device* device, std::size_t capacityBytes, std::uint32_t bindFlags) noexcept;
    void Shutdown() noexcept;

    // Usa D3D11_MAP_WRITE_NO_OVERWRITE; aplica D3D11_MAP_WRITE_DISCARD solo al dar la vuelta al anillo
    [[nodiscard]] bool Allocate(ID3D11DeviceContext* ctx, std::size_t sizeBytes, std::size_t alignment, RingAllocation& outAlloc) noexcept;
    void Unmap(ID3D11DeviceContext* ctx) noexcept;

    [[nodiscard]] ID3D11Buffer* GetBuffer() const noexcept { return m_buffer; }

private:
    ID3D11Buffer* m_buffer{nullptr};
    std::size_t   m_capacity{0};
    std::size_t   m_head{0};
};

} // namespace unc::graphics
```

```cpp
// include/unc/core/frame_pacer.hpp
#pragma once
#include <cstdint>
#include <windows.h>

namespace unc::core {

class FramePacer {
public:
    bool Initialize(double targetFps = 60.0) noexcept;
    void Shutdown() noexcept;

    void BeginFrame() noexcept;
    // Espera híbrida: High-Resolution Waitable Timer + _mm_pause() spin-lock final
    void WaitForTargetFrameTime() noexcept;

    void SetTargetFps(double targetFps) noexcept;
    [[nodiscard]] double GetLastFrameTimeMs() const noexcept { return m_lastFrameTimeMs; }

private:
    HANDLE       m_waitableTimer{nullptr};
    std::int64_t m_qpcFrequency{0};
    std::int64_t m_frameStartTicks{0};
    std::int64_t m_targetFrameTicks{0};
    double       m_lastFrameTimeMs{16.666};
};

} // namespace unc::core
```

---

## 7. Catálogo de APIs, Configuración `vcpkg` y Scripts de Compilación

### 7.1. Fuentes de Dependencias

| Subsistema | API / Librería | Método de Obtención | Propósito Técnico |
| :--- | :--- | :--- | :--- |
| **Gráficos (Ruta Principal)** | DirectX 11.1 + DXGI 1.4 | Windows 10/11 SDK | Máxima estabilidad de memoria en GPUs de $1\text{--}2\text{ GB}$, `DiscardResource` y telemetría de VRAM. |
| **Gráficos (Ruta Avanzada)** | DirectX 12 Agility SDK / Vulkan | NuGet (`Microsoft.Direct3D.D3D12`) / LunarG | Ruta opcional para hardware moderno con *async compute*. |
| **Compresión y Texturas** | DirectXTex | vcpkg (`directxtex:x64-windows`) | Conversión offline/online a BC1, BC4, BC5 y BC7 y generación de cadenas de *mipmaps*. |
| **Entrada y Mandos** | SDL3 / GameInput | vcpkg (`sdl3:x64-windows`) / NuGet | Soporte nativo DualShock 4 y DualSense sin capas de emulación pesadas. |
| **Hooking de Bajo Nivel** | MinHook | vcpkg (`minhook:x64-windows`) | Interceptación limpia de `IDXGISwapChain::Present`, `ResizeBuffers` y `WndProc`. |

### 7.2. Archivo `vcpkg.json`

```json
{
  "name": "uncharted-sic-parvis-magna-collection",
  "version-string": "1.0.0",
  "dependencies": [
    "minhook",
    "sdl3",
    "directxtex"
  ]
}
```

### 7.3. Configuración de Compilación de Alto Rendimiento (`CMakeLists.txt` base)

El proyecto debe compilarse con optimizaciones de punto flotante rápidas, generación de código intrínseco y eliminación de sobrecarga de RTTI/excepciones en los módulos de renderizado:

```cmake
cmake_minimum_required(VERSION 3.24)
project(UnchartedSicParvisMagna VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(
        /W4 /WX                 # Cero tolerancia a warnings
        /MP                     # Compilación multiprocesador
        /fp:fast                # Matemática SIMD optimizada para matrices y vectores
        /Oi /Ot                 # Priorizar funciones intrínsecas y velocidad de ejecución
        /GS-                    # Desactivar comprobaciones redundantes de buffer en hot-path validado
        /GR-                    # Desactivar RTTI para ahorrar memoria y evitar dynamic_cast
    )
    add_link_options(/OPT:REF /OPT:ICF /LTCG)
endif()
```

### 7.4. Script Maestro de Compilación (`tools/Build.ps1`)

```powershell
param (
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$RootPath = Resolve-Path "$PSScriptRoot\.."
$BuildDir = Join-Path $RootPath "build"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " UNCHARTED: Sic Parvis Magna - Build Pipeline ($Config|$Platform)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

if (!(Test-Path $BuildDir)) {
    Write-Host "[*] Configurando árbol de CMake..." -ForegroundColor Yellow
    $ToolchainArg = ""
    if ($env:VCPKG_ROOT) {
        $ToolchainFile = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        $ToolchainArg = "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
    }
    cmake -B $BuildDir -S $RootPath -A $Platform $ToolchainArg
}

Write-Host "[*] Compilando binarios nativos con optimización máxima..." -ForegroundColor Yellow
cmake --build $BuildDir --config $Config -- /verbosity:minimal /maxcpucount

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[+] COMPILACION EXITOSA. Binarios verificados sin warnings." -ForegroundColor Green
} else {
    Write-Host "`n[-] FALLO DE COMPILACION: Revisa los errores del compilador arriba." -ForegroundColor Red
    exit 1
}
```

---

## 8. Protocolo de Verificación y Telemetría antes de Hacer Commit

Ningún cambio en `native/graphics/` o `native/core/` se considera terminado hasta que el agente valide estos cuatro puntos:

1. **Cero Asignaciones por Fotograma:** El contador de memoria del sistema no debe crecer tras $10,000$ llamadas consecutivas a `Present`.
2. **Respeto del Límite de VRAM:** En simulación de perfil de $1\text{ GB}$ (`m_hardCapBytes = 859832320`) y $2\text{ GB}$ (`m_hardCapBytes = 1803550720`), el gestor `VramBudgetManager` mantiene el uso por debajo del límite sin invocar memoria compartida del sistema (`NonLocalUsage == 0`).
3. **Invariante de Ejes y Escalado:** Cambiar de $1920 \times 1080$ ($16:9$) a $1920 \times 1200$ ($16:10$) o $2560 \times 1080$ ($21:9$) conserva la precisión del ratón al píxel y no desplaza los iconos del HUD tras abrir y cerrar menús repetidamente.
4. **Compilación Limpia:** `.\tools\Build.ps1 -Config Release` finaliza con código de salida `0` y cero advertencias (`/WX`).