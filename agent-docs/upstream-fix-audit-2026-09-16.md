# Correcciones pendientes de Crosspoint y CrossInk — 2026-09-16

## Alcance

Comparación del código local de CPR-vCodex (`fddda85c`, más las correcciones
locales de OTA e imágenes #215) con referencias remotas recién consultadas:

- Crosspoint `origin/develop`: `3f874f8472280398c3d1900ab66450583c101e7a`.
- CrossInk `origin/main`: `7a092e8822c9c90e8beecacd317acc13d3e24dfb`.

Auditoría selectiva de estabilidad X3/X4, no inventario exhaustivo de todas
las diferencias. Se comprueba la implementación, no solo la presencia de un
commit en la historia. Los seis casos siguientes están pendientes por
comparación de código; no se han reproducido aún en hardware ni se han
aplicado en esta auditoría. Las referencias de `develop` no equivalen a una
release estable validada para nuestro fork.

## Seis correcciones confirmadas como pendientes

**Actualización posterior:** el punto 1 (JPEG #2925) ya está aplicado localmente,
con el parche 0003 idéntico a upstream y cinco pruebas del decodificador real.
Los casos de componentes separados y tabla Cr distinta fallan sin el parche
y pasan con él. La lista conserva la evidencia inicial; los otros cinco
puntos siguen pendientes. Ver `test/jpegdec/README.md`.

Validación de JPEG #2925: 274/274 pruebas nativas, compilaciones `default` y
`gh_release` correctas. La biblioteca instalada por PlatformIO también supera
los cinco casos con AddressSanitizer en WSL (`-no-pie`). La primera variante
PIE no terminó y repetía `DEADLYSIGNAL`; no se cuenta como validación. La
variante `-no-pie`, con límite de tiempo por caso, completó los cinco sin errores.
BIN de desarrollo: `artifacts/1.6.0.38.dev3-fddda85c-cpr-vcodex.bin`.
La compilación de producción se hizo con `-t buildprog` sin empaquetar ni
sobrescribir el candidato .38 anterior: 6.103.776 bytes, 449.824 libres en
el slot X4. Logs en `artifacts/jpeg2925/`. No se ha flasheado ni publicado.

| Orden | Corrección de Crosspoint | Evidencia en CPR-vCodex | Comprobación necesaria al adaptarla |
|---|---|---|---|
| 1 | [JPEG progresivos con componentes en scans separados, #2925](https://github.com/crosspoint-reader/crosspoint-reader/commit/6230eba2d8b757b2d480d5a151b1786d0537c7a6) | `scripts/jpegdec_patches/` contiene 0001 y 0002, pero no 0003. El JPEGDEC usado por `default` todavía consume ambos componentes cromáticos sin comprobar `component_needed`. Puede fallar con esos JPEG, incluidas portadas. Es distinto del fallo de gris #215. | Muestra JPEG con scans separados; comprobar JPEGDEC real, no solo el simulador que usa otro decodificador. Mantener las dos protecciones anteriores. |
| 2 | [Rutas y nombres en el gestor web, #3353](https://github.com/crosspoint-reader/crosspoint-reader/commit/03c484778bc6d4eb5376c7210b69d8d33aaee13e) | `FilesPage.html` interpola nombres/rutas en `onclick`; solo escapa apóstrofos. El servidor normaliza mover/renombrar, pero listar, descargar, subir y crear carpeta conservan tratamiento parcial. | Nombres con caracteres especiales, navegación de rutas y operaciones sobre SD virtual; mantener las protecciones de archivos y funciones propias del fork. |
| 3 | [Precisión al subir progreso a KOReader Sync, #3174](https://github.com/crosspoint-reader/crosspoint-reader/commit/472b5e485f9f55864133a2f64dd25256749041d8) | `ProgressMapper::toSavedProgress()` usa párrafo o porcentaje; no aprovecha `hasVisibleTextOffset`. Falta `ChapterXPathResolver::findXPathForVisibleTextOffset()`. Puede perder precisión dentro de un párrafo largo. | Ida y vuelta de posiciones con cambios de fuente/orientación, nodos anidados y texto multibyte; conservar nuestras estadísticas, sesión de sync y progreso persistido. |
| 4 | [Pulsaciones perdidas al repintar listas, #3534](https://github.com/crosspoint-reader/crosspoint-reader/commit/483c5cf6) | `UiListActivity::moveSelectionTo()` sigue tomando `RenderLock`; `UiTabListActivity::moveRingTo()` modifica el viewport inmediatamente. La espera del refresco puede impedir muestrear otra pulsación corta. | Secuencias rápidas en botones reales X3/X4; revisar propiedad y sincronización de la selección antes de retirar bloqueos. |
| 5 | [Respetar el atributo HTML `hidden`, #3390](https://github.com/crosspoint-reader/crosspoint-reader/commit/6eda8f0b7f204b7c51f8f79ddfd37bed688a74df) | `ChapterHtmlSlimParser` procesa clase, estilo y dirección, pero no el atributo booleano `hidden`. Puede mostrar contenido editorial que el EPUB marca como oculto. | Portar casos de prueba upstream, incluidos elementos anidados y `hidden="false"` (también oculto por ser booleano). No confundirlo con `aria-hidden`, que no oculta contenido visual. Revisar invalidación de caché de maquetación. |
| 6 | [Sincronizar la selección al terminar un libro, #3418](https://github.com/crosspoint-reader/crosspoint-reader/commit/da7feed5c7e777b2bb9be6e0848c9e42a5faea29) | `EndOfBookOptions::selector` sigue siendo un `int` compartido entre entrada y render; upstream lo convierte en atómico y toma una instantánea para procesar la acción. | Cambiar selección mientras se repinta; confirmar que se abre el libro seleccionado y funcionan volver/Home. Adaptación pequeña. |

## CrossInk: siguiente mejora a evaluar

CrossInk inicia OTA, OPDS, autenticación/sincronización KOReader, transferencia
de archivos y gestión de fuentes mediante un arranque de red ligero. Véanse
[los destinos y API](https://github.com/uxjulia/CrossInk/blob/7a092e8822c9c90e8beecacd317acc13d3e24dfb/src/SilentRestart.h)
y las llamadas en `ActivityManager.cpp`, `KOReaderSettingsActivity.cpp` y
`KOReaderSyncActivity.cpp` del mismo commit.

Nuestro cambio local aplica ese patrón a OTA. Para OPDS y KOReader Sync,
`ActivityManager` todavía cambia de actividad dentro del mismo arranque.
**Falta la estrategia completa, pero eso no demuestra por sí solo un fallo
actual:** ya liberamos recursos al cambiar de actividad. Primero medir memoria
contigua y reproducir una conexión tras leer un EPUB pesado; después decidir
si conviene extender el arranque ligero, conservando la posición y el destino
de regreso. No copiar el sistema de manifiestos ni los nombres de CrossInk.

## Cambios de pantalla: revisión separada

[Crosspoint #3439](https://github.com/crosspoint-reader/crosspoint-reader/commit/1f3d7458a77c31641581667887029a5384ac4d3c)
añade preparación de los grises del X3 y restringe el solapamiento de refresco,
con cambio del SDK. Nuestro lector usa una ruta propia de refresco y el HAL
ya fuerza resincronización en HALF para X3; no tiene la secuencia completa de
ese cambio. No son equivalentes, pero tampoco conviene importar el commit
sin aislar las diferencias y verificar los paneles X3 correspondientes.
El X4 disponible no valida el comportamiento físico de un X3.

También requieren evaluación aparte la reducción reciente de fragmentación de
imágenes ([#2332](https://github.com/crosspoint-reader/crosspoint-reader/commit/3555ff55))
y el cambio general de grises absolutos/SDK. No mezclar esas modificaciones
amplias con la corrección mínima de #215.

## Correcciones que ya tenemos

- `c4d8c395` / #3527: `Section::startBuild()` libera las cachés de fuentes SD
  antes de reservar memoria para CSS y maquetación.
- `c80c537f` / #3521: preparación de caché de fuentes con `accumulate=false`
  para el conjunto de glifos de la página, evitando acumulación innecesaria.
- `9ba16086` / #3463: el bucle de reposo comprueba contactos de botones en
  intervalos cortos con `rawInputActive()`.
- El lector ZIP ya usa `InflateStream` y comprueba errores de lectura; no
  arrastra el callback antiguo basado en reinterpretar `uzlib_uncomp`.
- Imágenes independientes del suavizado de texto (#2393/#215): corrección
  local comprobada en simulador, todavía pendiente de validación física y
  publicación. Ver `issue-215-image-grayscale.md`.
- Arranque de red para OTA: corrección local validada en un X4 recuperable,
  todavía pendiente de publicación. Ver `stability-audit-2026-09.md`.

## Orden de trabajo

Reproducir y adaptar cambios pequeños por separado: JPEG, gestor web y
posición KOReader; después interacción/listas, `hidden` y selección final.
Cada adaptación necesita su regresión, compilación C3 y validación apropiada.
Conservar los cambios locales ya probados. Mantener SDK, particiones, OTA y
pantallas fuera de una mezcla general de ramas. Esta auditoría no cambia
firmware, no flashea y no publica.
