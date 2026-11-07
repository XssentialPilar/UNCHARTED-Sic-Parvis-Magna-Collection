# Créditos y procedencia

Esta es una variante de desarrollo de Uncharted Collection PC. Los cambios de rendimiento y
entrada de este fork se registran en `PERFORMANCE-REVIEW.md`. La procedencia del código y los
avisos originales de licencia se conservan aquí y en las cabeceras de cada componente.

- Base: **BilalReloaded**, Uncharted: The Nathan Drake Collection PC port, versión 0.8.1,
  commit `25f988a3302c6cfe263ea732e419a7a8c9c207ac`.
  Fuente: https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port.
  Incluye su runtime, launcher y correcciones de los tres juegos; GPL v2 o posterior.
- **SuperMedo / Mohammed Albarghouthi**, `uncharted_pc` 0.3.13.
  Fuente: https://github.com/Supermedo/uncharted_pc.
  Adaptaciones en este fork: consulta conjunta de uso/presupuesto de memoria, contabilidad de
  imágenes pendientes de destrucción y conservación del dispositivo/tamaño al mover `UniqueImage`.
  La base ya incorporaba su seguimiento de cámara para antialiasing temporal. GPL v2 o posterior.
- **Felipe Prado y colaboradores**, SpartanPort 3.0.0.
  Fuente: https://github.com/FelipePrado0/SpartanPort.
  Referencia para confirmar la captura del ratón solamente cuando SDL informa éxito; se añadió
  recuperación y una prueba de errores/reintentos específica para este fork. GPL v2 o posterior.
- **deadinside28**, `bloodborne_pc`, y **shadPS4 Emulator Project**: bases de ejecución nativa,
  recompilador de sombreadores y renderer Vulkan. Sus avisos SPDX y copyright permanecen en los
  archivos correspondientes.
- Las bibliotecas de terceros mantienen sus avisos: AMD FidelityFX/FSR, NVIDIA DLSS, FFmpeg,
  SDL3, LibAtrac9, Dear ImGui, sirit, magic_enum, miniz, xbyak y las demás dependencias.
  Véanse `gow3_pc/LICENSE`, las licencias dentro de cada dependencia y el directorio `licenses`
  del paquete ejecutable.

Los juegos, sus nombres y sus recursos pertenecen a sus respectivos titulares. Esta variante no
está afiliada a Sony, Naughty Dog ni Bluepoint. Los archivos del juego no se distribuyen aquí.
