# Rendimiento y estabilidad — trabajo del 5 de noviembre de 2026

## Objetivo y medida

Se busca reducir la variación y los picos del tiempo de fotograma al límite elegido por el jugador,
hasta 120 FPS: 33,33 ms a 30, 16,67 ms a 60, 11,11 ms a 90 y 8,33 ms a 120. Cada resultado debe
identificar resolución, escena, caché fría/caliente y límite. Los FPS medios no demuestran fluidez.
Tampoco se puede garantizar el mismo límite en todo hardware o escena.

## Base y registros recibidos

Los ZIP `main` y `0.8.1` contienen los mismos 1.074 archivos. La base corresponde al commit
`25f988a3302c6cfe263ea732e419a7a8c9c207ac`. Los registros son de versiones anteriores y se usan
para localizar síntomas, no como una comparación A/B del nuevo ejecutable.

| Registro | Muestras de memoria | Pico VRAM MiB | Pico arenas MiB | Mediana de FPS registrados | Peor p99 de ventana, ms | Mayor tirón registrado, ms |
|---|---:|---:|---:|---:|---:|---:|
| U1 | 539 | 3254 | 1344 | 82,3 | 51,04 | 164,9 |
| U2 | 353 | 4786 | 2043 | 68,8 | 62,59 | 164,9 |
| U3 | 1119 | 5193 | 2174 | 96,2 | 138,69 | 235,0 |
| U2 0.7.2 MT | 743 | 4480 | 1984 | 82,7 | 516,93 | 846,2 |

El presupuesto registrado es 7142 MiB. Ninguna muestra de estos archivos supera ese presupuesto:
son evidencia de crecimiento y tirones, no una demostración de agotamiento de VRAM. El contador de
imágenes refleja tamaño del recurso invitado; no equivale a la asignación física completa.

U3 registra 451 MiB de arenas reciclados, mientras la reserva física sigue en 2174 MiB.
El código confirma que los rangos se apartaban para reutilizarlos sin llamar a `vkFreeMemory`.
El cierre de U2 MT muestra `ReadInvalid` y `ErrorDeviceLost`; su causa no se clasifica como OOM.

## Cambios C/C++

1. Arenas: se registran las asignaciones físicas y se retiran únicamente cuando todos sus bytes
   aparecen en rangos reutilizables. Los rangos con desvinculaciones pendientes quedan en cuarentena.
   La liberación espera la señal del scheduler y ocurre fuera del hilo que procesa los dibujos.
   Los fragmentos finales de chunks que antes se perdían pasan a ser reutilizables.
2. Recolector: aumentar la presión admite recursos más recientes (160/80/16 ticks), en lugar de
   estrechar los candidatos. Se evita desbordar la resta de ticks al arrancar y se actualiza el
   contador de samplers después de eliminarlos.
3. Imágenes: se descuentan las asignaciones cuya destrucción ya está programada. Las restas se
   saturan a cero. Los movimientos de `UniqueImage` conservan dispositivo y tamaño físico.
4. Uso/presupuesto de VRAM: una consulta al controlador obtiene ambos valores coherentemente,
   conservando el intervalo de consulta de la base cuando no hay presión.
5. Caché: una cola FIFO con un único mutex sustituye dos mutex y un contador compartido sin
   sincronización. El cierre drena las escrituras aceptadas. Los datos pasan por movimiento;
   se elimina otra copia en el constructor de los archivos de serialización.
6. Ratón: captura confirmada por SDL, reintentos con pausa de 250 ms al fallar, recuperación de
   modo relativo perdido, alternativa de foco nativo de Windows y estado compartido atómico.
   Menús, pérdida de foco y desactivación liberan botones y acumuladores.
7. Fork: se retiran títulos y enlaces promocionales del launcher; los créditos quedan en
   `CREDITS.md`. La marca `PERFORMANCE_FORK` evita que el actualizador sustituya esta variante
   por una compilación del canal original.

Los parámetros de liberación de arenas existentes (`GOW3_ARENA_LIMIT_MB`,
`GOW3_ARENA_RECLAIM_AGE_S`) siguen siendo umbrales de caché. No son límites duros de VRAM.
La fragmentación puede impedir liberar un chunk que todavía contenga datos vivos. No se eliminan
datos escritos por la GPU que no tengan copia válida en RAM.

## Comparación de referencias

| Referencia | Aprovechado | Conservado fuera de esta integración |
|---|---|---|
| SuperMedo 0.3.13 | Consulta conjunta, destrucciones pendientes, movimientos de imágenes | Su estrategia de arenas también conserva asignaciones; no sustituye la liberación añadida aquí. La corrección de fuga de generación de cuadros no aplica a una ruta que no la usa. |
| SpartanPort 3.0.0 | Confirmación real de captura SDL y patrón de pruebas de entrada | Parches de instrucciones y tasas del motor de God of War no se aplican a Uncharted. Su GC conserva la antigüedad invertida. Sus pools y preparación paralela ya tienen equivalentes en la base. |

## Validación

Las pruebas y sus registros finales se guardan fuera del código, en `../Registros` y `../Pruebas`.
No se atribuye una mejora de FPS a cambios que solo se han probado de forma funcional.
La primera prueba de arranque U2, anterior a la última revisión de ratón, funcionó 90 segundos
con un perfil separado y caché inicialmente vacía; terminó por el temporizador del arnés.
Al estabilizarse el menú registró 60 FPS y p99 de ventana próximo a 17,09 ms. Esto solo valida
ese arranque/menú, no una partida larga ni todas las escenas.

Queda registrar la validación final de este ejecutable y realizar A/B de la misma escena con
la misma caché, resolución, límite y recorrido antes de afirmar una ganancia de rendimiento.
