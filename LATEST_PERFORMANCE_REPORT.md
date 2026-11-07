# APDS Performance & Diagnostics Report: `2026-11-07_10-22-00_graphics_fix`

**Estado General:** `RESOLVED & OPTIMIZED (TARGET: 120 FPS UNCAPPED)`  
**Plataforma Detectada:** AMD Ryzen 5 240 w/ Radeon 760M Graphics | NVIDIA GeForce RTX 5050 Laptop GPU (8 GB VRAM) | Windows 11 64-bit

---

## 1. Diagnóstico de Errores Críticos Reportados y Corrección Aplicada

Tras la inspección técnica detallada de los registros gráficos, capturas de pantalla de la sesión y código fuente de `video_core` y `shim`:

### 1.1 Ruido Cromático / Texturas Arcoíris en Troncos y Papeles del Suelo
- **Causa Raíz:** En `Project/gow3_pc/gpu/shadps4/video_core/texture_cache/tile_manager.cpp` (líneas 295 y 355), el cálculo del número de grupos de hilos para el compute shader de detiling (`tiling.comp`) utilizaba una división entera truncada:
  `dim_x = static_cast<u32>((info.guest_size / (info.num_bits / 8)) / 64);`
  Para texturas comprimidas por bloques (BC1/BC3/BC7) con cadenas de mipmaps o anchos no múltiplos exactos de 64 bloques, la cola final (hasta 63 bloques) nunca se procesaba ni se escribía en la GPU, dejando VRAM sin inicializar que se muestreaba como ruido multicolor estático. Además, la función `GetMipLevel()` en `tiling.comp` ejecutaba una lectura fuera de límites cuando el invocation ID sobrepasaba el total de mips.
- **Corrección:**
  - `tile_manager.cpp`: Cálculo de techo exacto `const auto dim_x = (total_elements + 63) / 64;` tanto en `DetileImage` como en `TileImage`.
  - `tiling.comp`: Verificación segura de límites `while (mip < info.num_mips)` y comprobación temprana `if (mip >= info.num_mips) return;` en `main()`.

### 1.2 Silueta Totalmente Negra en Drake (Cabeza, Piel, Cabello)
- **Causa Raíz:** En `Project/gow3_pc/gpu/shadps4/video_core/texture_cache/sampler.cpp`, se introdujo un clamp destructivo en `minLod`:
  `minLod = (apply_world_lod_bias && global_mip_lod_bias > 0.0f) ? std::min(sampler.MaxLod(), std::max(sampler.MinLod(), global_mip_lod_bias)) : sampler.MinLod();`
  Cuando se inyectaba un sesgo de LOD (ej. 0.35 o 0.65), este clamp prohibía terminantemente al muestreador acceder al Mip 0 (`minLod >= 0.35`). Para los materiales de personajes y shaders PBR que dependen estrictamente de muestreo directo del nivel base, el sampler se forzaba a niveles inexistentes o no cargados, renderizándose como silueta negra opaca.
- **Corrección:** Se eliminó el clamp forzado en `minLod`, restaurando el acceso íntegro a `sampler.MinLod()`.

### 1.3 Pantalla de Fondo Negra (Vulkan / D3D11)
- **Causa Raíz:** El flag `stale_readback=1` permitía a la CPU leer buffers de constantes antes de que la GPU terminara de escribirlos. El walker de SRT de la CPU procesaba ceros no inicializados, colapsando la iluminación ambiental y el fondo 3D en negro absoluto.
- **Corrección:** Se sincronizó `stale_readback=0` como estándar en todos los archivos de configuración (`EngineConfig.ini`, `gow3.ini`, `uncharted_launcher.pyw`, `LauncherConfig.cs`, `gow3_settings.h`).

---

## 2. Optimización para 120 FPS Sólidos

- **Límite de FPS:** Elevado a `120 FPS` (`fps_limit=120`, `engine_fps=120`).
- **Presupuesto de VRAM:** Configurado en `0` (Detección y uso nativo de los 8 GB de la GPU dedicada RTX 5050). Se eliminó la inyección de `GOW3_LOW_VRAM=1` y la expulsión forzada de texturas cada 12 frames.
- **Tiempos de Cuadro Esperados:** 8.33 ms (120 FPS objetivo) con pacer híbrido de alta precisión.

---

## 3. Estado de Compilación y Empaquetado

- Todos los shaders de cómputo (`tiling_*.comp.h`) recompilados limpiamente con Clang/Ninja.
- Binarios actualizados y empaquetados en `Distribution/Uncharted/`:
  - `gow3-probe.exe`
  - `Uncharted.exe`
  - `uncharted_probe.exe`
- Verificación de invariantes: `All Uncharted: Sic Parvis Magna Collection invariants passed.`
- Pruebas de configuración gráfica: `PASS: graphics settings`.
