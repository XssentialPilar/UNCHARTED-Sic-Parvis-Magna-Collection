# UNCHARTED: Sic Parvis Magna Collection
> Runtime layer, display adaptation, and low-level reverse engineering project for PC.

---

## 1. Directrices de Desarrollo para Agentes de IA (Antigravity / Argon / Codex)

Este repositorio está optimizado para flujos agénticos automatizados y razonamiento en contexto profundo. Cualquier modelo de lenguaje que opere en este código debe ceñirse a las siguientes normas operativas:

### 1.1. Filosofía de Implementación y Rol
* **Rol:** Actúas como un ingeniero de sistemas sénior especializado en desarrollo de motores gráficos, C++ de bajo nivel, APIs gráficas (DirectX 11/12, Vulkan) e interactividad Win32.
* **Tolerancia Cero a APIs Alucinadas:** Está estrictamente prohibido invocar funciones, métodos o estructuras de bibliotecas externas que no existan explícitamente en el árbol de cabeceras (`vendor/` o `include/`). Si no encuentras una definición, inspecciona primero el archivo de cabecera local correspondiente antes de escribir la llamada.
* **Entrega Completa de Archivos:** Al generar o refactorizar código, genera siempre el archivo completo de principio a fin. No omitas bloques de código ni utilices marcadores de posición como `// ... resto del código ...`.
* **Depuración Focalizada en Errores Reales:** Al interactuar con el compilador de MSBuild o CMake, analiza el código de error exacto (ej. `E0020`, `C2039`, `LNK2019`) y rastrea la cabecera canónica antes de sugerir reescrituras innecesarias.

### 1.2. Estándares de Código C++
* **Estándar:** C++20 / C++23.
* **Gestión de Memoria:** RAII estricto. Utiliza punteros inteligentes (`std::unique_ptr`, `std::shared_ptr`) para la propiedad de recursos y punteros no propietarios (`raw pointers`) únicamente para referencias o vistas de contexto.
* **Validación de APIs de Windows y DXGI:** Toda llamada que retorne un código de estado `HRESULT` debe validarse de forma explícita mediante la macro `THROW_IF_FAILED` o estructuras de manejo de errores equivalentes.
* **Seguridad de Tipos:** Emplear `enum class` fuertemente tipados, tipos de tamaño explícito (`uint32_t`, `int64_t`, `size_t`) y calificar con `const` y `noexcept` siempre que corresponda.

---

## 2. Invariantes del Pipeline Gráfico y de Entrada

Cualquier cálculo relacionado con resoluciones, transformaciones de matrices o manejo de ratón debe cumplir rigurosamente estos tres invariantes matemáticos:

### 2.1. Desacoplamiento de Swapchain y Viewport
* El búfer interno de renderizado (Backbuffer) y el área de presentación del cliente (`HWND`) son entidades desacopladas.
* El factor de aspecto (aspect ratio) nunca debe ser modificado mediante una mutación destructiva del swapchain en cada fotograma para evitar bucles de retroalimentación (*feedback loops*) en DXGI.
* La relación de aspecto se define mediante:
  $$\text{Aspect Ratio} = \frac{\text{Width}_{\text{logical}}}{\text{Height}_{\text{logical}}}$$
* Toda compensación por ultrapanorámico (21:9, 32:9) o pantallas 16:10 debe realizarse en las matrices de proyección y en los viewports de tijera (*scissor rects*), manteniendo estables las dimensiones base del swapchain.

### 2.2. Remapeo de Coordenadas de Entrada (Input Pipeline)
* Para prevenir desincronizaciones de cursor (*double cursor* o *offset drift*), las coordenadas del ratón capturadas por el sistema operativo ($X_{\text{screen}}, Y_{\text{screen}}$) deben normalizarse al espacio de coordenadas del juego ($X_{\text{game}}, Y_{\text{game}}$):
  $$X_{\text{game}} = (X_{\text{client}} - \text{Viewport}_X) \times \left( \frac{\text{Width}_{\text{internal}}}{\text{Viewport}_{\text{width}}} \right)$$
  $$Y_{\text{game}} = (Y_{\text{client}} - \text{Viewport}_Y) \times \left( \frac{\text{Height}_{\text{internal}}}{\text{Viewport}_{\text{height}}} \right)$$
* La propiedad de los ejes $X$ e $Y$ debe mantenerse desacoplada: las transformaciones aplicadas a un eje no deben contaminar las variables de estado del eje opuesto durante transiciones o animaciones de menús e inventarios.

### 2.3. Ciclo de Interceptación (Hooking) en Presentación
El flujo de ejecución en el hook de `IDXGISwapChain::Present` o `Present1` debe seguir estrictamente este orden:
1. Lectura segura de descriptores de swapchain (`GetDesc` / `GetDesc1`).
2. Comprobación y limpieza de marcas de modificación (*dirty flags*).
3. Aplicación de ajustes de proyección y escalado de interfaces (UI).
4. Renderizado de overlays propios.
5. Invocación al puntero de función original de `Present`.

---

## 3. Arquitectura del Repositorio

El proyecto mantiene una estructura modular y autocontenida que previene indexaciones innecesarias de archivos binarios pesados:

```text
UNCHARTED-Sic-Parvis-Magna-Collection/
├── .antigravity/
│   └── rules                     # Instrucciones y restricciones para el agente
├── docs/
│   ├── ARCHITECTURE.md           # Diagramas y notas de arquitectura de bajo nivel
│   └── INPUT_PIPELINE.md         # Documentación de fórmulas matemáticas y calibración
├── include/
│   └── unc/                      # Cabeceras públicas exportadas por el proyecto
│       ├── core.hpp
│       ├── graphics.hpp
│       └── input.hpp
├── native/                       # Implementación interna en C++
│   ├── core/                     # Hooks base, inyección DLL, utilidades de memoria
│   ├── graphics/                 # Backends de renderizado (DX11, DX12, Vulkan)
│   │   ├── dx11_backend.cpp
│   │   ├── dx12_backend.cpp
│   │   └── swapchain_hook.cpp
│   └── input/                    # Mapeo de mandos (DualShock/DualSense) y ratón
│       ├── gameinput_handler.cpp
│       └── sdl_handler.cpp
├── vendor/                       # Cabeceras limpias sin binarios pesados (Headers Only)
│   ├── d3d12/                    # Cabeceras del DirectX 12 Agility SDK (d3d12.h, etc.)
│   ├── minhook/                  # MinHook header (minhook.h)
│   └── sdl3/                     # Cabeceras de SDL3
├── tools/
│   ├── Build.ps1                 # Script maestro de compilación con CMake/MSBuild
│   └── Package.ps1               # Script de verificación de hashes y empaquetado
├── CMakeLists.txt                # Configuración de compilación CMake
├── vcpkg.json                    # Manifiesto de dependencias vcpkg
└── README.md                     # Documentación general del repositorio
```

---

## 4. Elementos de API y Fuentes de Dependencias

Para compilar el proyecto sin discrepancias de entorno, se utilizan las siguientes APIs oficiales y métodos de integración:

| Subsistema | API / Librería | Método de Obtención / Instalación | Archivos Clave |
| :--- | :--- | :--- | :--- |
| **Gráficos (Base)** | DirectX 11 / DXGI | Windows 10/11 SDK (Visual Studio Installer) | `<d3d11.h>`, `<dxgi1_6.h>`, `d3d11.lib`, `dxgi.lib` |
| **Gráficos (Avanzado)** | DirectX 12 Agility SDK | NuGet: `Microsoft.Direct3D.D3D12` | `D3D12Core.dll`, `<d3d12.h>`, variables exportadas |
| **Gráficos (Moderno)** | Vulkan SDK | LunarG (`vulkan.lunarg.com`) o vcpkg (`vulkan`) | `<vulkan/vulkan.h>`, `vulkan-1.lib`, capas de validación |
| **Utilidades de Texturas** | DirectXTex | GitHub: `microsoft/DirectXTex` o vcpkg (`directxtex`) | `DirectXTex.h`, decodificadores BC6H/BC7 y DDS |
| **Entrada (Recomendado)** | SDL3 | GitHub: `libsdl-org/SDL` o vcpkg (`sdl3:x64-windows`) | `<SDL3/SDL.h>`, captura DualShock/DualSense nativa |
| **Entrada (Windows Nativo)**| Microsoft GameInput | Windows SDK o NuGet (`Microsoft.GameInput`) | `<GameInput.h>`, latencia ultrabaja para periféricos |
| **Almacenamiento e I/O** | DirectStorage | NuGet: `Microsoft.Direct3D.DirectStorage` | `<dstorage.h>`, `dstorage.lib`, `dstorage.dll` |
| **Audio** | XAudio2 / miniaudio | Windows SDK (`<xaudio2.h>`) o GitHub (`miniaudio.h`)| Salida PCM multipista y soporte de audio envolvente |
| **Hooking de Funciones** | MinHook | GitHub: `TsudaKageyu/minhook` o vcpkg (`minhook`) | `minhook.h`, `minhook.lib` |

### 4.1. Configuración con vcpkg (`vcpkg.json`)
Crea un archivo `vcpkg.json` en la raíz para resolver dependencias automáticamente con CMake:

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

---

## 5. Pipeline de Compilación y Automatización

Para evitar ejecuciones erráticas en la consola interactiva, el proceso de compilación está centralizado en el script de PowerShell `tools/Build.ps1`.

### 5.1. Código del Script `tools/Build.ps1`
```powershell
param (
    [string]$Config = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$RootPath = Resolve-Path "$PSScriptRoot\.."
$BuildDir = Join-Path $RootPath "build"

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Iniciando compilacion: $Config | $Platform " -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan

if (!(Test-Path $BuildDir)) {
    Write-Host "[*] Generando archivos de proyecto con CMake..." -ForegroundColor Yellow
    cmake -B $BuildDir -S $RootPath -A $Platform -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
}

Write-Host "[*] Compilando binarios..." -ForegroundColor Yellow
cmake --build $BuildDir --config $Config -- /verbosity:minimal /maxcpucount

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[+] COMPILACION COMPLETADA SATISFACTORIAMENTE." -ForegroundColor Green
} else {
    Write-Host "`n[-] ERROR CRITICO: La compilacion fallo. Revisa el registro anterior." -ForegroundColor Red
    exit 1
}
```

### 5.2. Instrucciones de Compilación Local
1. Abrir terminal de PowerShell en el directorio raíz.
2. Ejecutar el compilador:
   ```powershell
   .\tools\Build.ps1 -Config Release -Platform x64
   ```
3. Los artefactos generados quedarán almacenados en la carpeta `build/bin/Release/`.

---

## 6. Flujo de Trabajo en Git

Para mantener los commits ordenados y consistentes:
* **Commits Semánticos:**
  * `feat(graphics): implement DirectX 11 swapchain hooks`
  * `fix(input): resolve cursor offset drift on 16:10 resolutions`
  * `refactor(core): decouple viewport scaling from client window bounds`
  * `docs(readme): update build dependencies and API instructions`