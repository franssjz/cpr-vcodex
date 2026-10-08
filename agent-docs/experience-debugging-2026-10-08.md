# Depuracion de la experiencia de CPR vCodex

8 de octubre de 2026. Revision del arbol de trabajo de
`integration/crosspoint-2026-10-07`, con la integracion de upstream pendiente
de commit. Esta ronda corrige discrepancias funcionales entre el fork y las
pantallas incorporadas de Crosspoint, y amplia las regresiones automatizadas.
No certifica que todo el firmware este libre de fallos.

## Correcciones de esta ronda

| Problema | Correccion |
| --- | --- |
| Guardar una marca desde la barra podia borrar una marca existente | La lista y la barra usan la misma operacion de guardado. El atajo de alternar marca se conserva. |
| Focus Reading solo permitia dos estados del ajuste Bionic Reading | Editor de texto y barra permiten Apagado, Normal y Sutil, con el mismo nombre y catalogo que los ajustes del fork. |
| La muestra trataba Sutil como Normal | La vista previa usa el modo real del motor de lectura. Su cache distingue los tres modos y la sangria forzada. |
| Bookerly aparecia como Noto Serif en las pantallas nuevas | Catalogo comun de nombres sin cambiar los indices persistidos. |
| Ajustes rapidos omitian Nunca en frecuencia de refresco | Las seis opciones comparten catalogo con Ajustes globales y se recorren correctamente. |
| El editor con vista previa no descubria fuentes SD al entrar directamente desde un libro con fuente integrada | Se actualiza el registro de fuentes antes de construir el listado. |
| Una fuente SD defectuosa dejaba etiquetas y tamanos de la fuente fallida | Tras la carga se reconstruyen familia y tamanos a partir del fallback efectivo, tambien al cambiar de tamano. |
| La recarga de fuente desde el lector podia liberar glifos mientras pintaba otra tarea | La recarga queda dentro del bloqueo de renderizado. Es una correccion preventiva por inspeccion, no un cuelgue reproducido. |
| El menu de lectura no utilizaba el reajuste de listas con filas altas | Delega el renderizado en la base compartida de listas. |
| Girar el menu a horizontal podia ocultar la fila seleccionada | El cambio de orientacion vuelve a solicitar seguimiento de seleccion antes de mostrar el nuevo viewport. Reproducido en Lyra Custom. |
| Tras detener el avance automatico, el panel seguia mostrando la velocidad anterior como activa | El valor y la seleccion del popup muestran Desactivado mientras el avance no esta activo. |
| Faltaban textos en espanol y Sync Day tenia una etiqueta poco clara | Anadidas 38 claves ausentes; Sync Day pasa a Sincronizar fecha y conserva sus funciones. El generador informa de cero claves con fallback al ingles en espanol. |

Los catalogos comunes estan en `src/ReaderSettingLabels.h`, con comprobaciones
de cardinalidad contra los enums persistidos. No se han eliminado funciones
propias del fork ni migrado sus valores guardados.

## Regresiones comprobadas

- **575 tests nativos, todos correctos.** Incluyen lectura, posicion,
  fuentes/cache, parsers, idiomas, listas, popups, sincronizacion, plugins,
  rutas protegidas, empaquetado y logica OTA simulada. Se anaden dos tests
  especificos para la clave de cache de la vista previa.
- **47 escenarios de navegacion cubiertos por bloques en el simulador X4.**
  Los 18 destinos de Apps, Plugins incluido; cuatro acciones de Sync Day;
  otras listas del fork; listas cortas y menu de lectura en Classic/Lyra
  Custom, ingles/espanol, con giro y seleccion visible. Tras corregir el giro,
  se repitieron los 16 escenarios de menus y ajuste de altura.
- **26 escenarios de ajustes y lectura correctos en una ejecucion completa
  con el simulador reconstruido desde cero.** Se comprueban
  JSON persistido, contador binario de marcas y pixeles de las capturas:
  guardado idempotente, seis frecuencias, tres modos bionicos por tres rutas,
  cancelacion, selectores de fuente, fuente SD corrupta, muestras distintas
  y estado del avance automatico detenido.
  Ademas se repiten cuatro casos visuales en espanol con Classic.
- **Cppcheck de todo el entorno default:** cero defectos de severidad alta.
  Siguen existiendo avisos medios y bajos; este resultado no significa cero
  avisos ni demuestra ausencia de errores de memoria en el dispositivo.
- **Compilaciones limpias default y gh_release correctas, con cache nueva.** La segunda se ejecuto con
  `VCODEX_RELEASE_DRY_RUN=1`, sin avanzar el contador de releases ni publicar.
- `git diff --check` correcto. El SDK permanece sin modificaciones locales.

Las pruebas usan libros sinteticos y SD desechables. Las entradas de prueba y
trazas se aplican solo a una copia aislada y se retiran al terminar. El
firmware de dispositivo no contiene esa instrumentacion.

Una compilacion incremental del simulador presento cabeceras y muestras
vacias. El mismo codigo, reconstruido desde cero con una cache nueva, vuelve
a pintarlas correctamente y pasa la matriz completa. Esto apunta a objetos
de compilacion antiguos; no se ha identificado una causa adicional en el
renderizador. Los tests ahora exigen contenido visible en cabecera y muestra,
ademas de comparar capturas, para no aceptar dos regiones igualmente vacias.

La cobertura de esta ronda vuelve a comprobar los arreglos anteriores de
indices desplazados en Apps/Sync Day y de altura de listas. Los tests nativos
de los popups tambien se repiten; las pruebas de estres de 40 opciones y 35
lineas pertenecen a la ronda anterior, no a una nueva ejecucion completa aqui.

## Compilaciones locales

| Entorno | Version interna | RAM estatica | BIN empaquetado | Margen en slot X4 |
| --- | --- | --- | --- | --- |
| default | 1.6.0.38.dev34-a4ae01a7 | 61.936 B | 6.351.808 B | 201.792 B |
| gh_release dry run | 1.6.0.39 | 61.920 B | 6.274.560 B | 279.040 B |

Slot X4: 6.553.600 bytes. El BIN de produccion ocupa aproximadamente el 95,7 %;
el margen es reducido. La RAM estatica no incluye el consumo dinamico durante
la lectura. Ambos binarios estan en `artifacts/`; son resultados locales de
verificacion, no una release publicada ni una autorizacion para distribuirlos.

## Pendiente de validacion fisica

1. Probar en X4 con copia de seguridad: abrir/cerrar libros reales, cambiar
   fuentes y tamanos, regresar a la posicion, reiniciar y verificar persistencia.
2. Confirmar refresco y ghosting en e-ink, autonomia, suspension y reanudacion.
3. Probar Wi-Fi/NTP, KOReader, OPDS, OTA y plugins contra servicios reales.
   Pasar tests de logica o abrir la actividad no valida la transaccion completa.
4. Verificar otras SD y libros grandes o con contenido no representado por los
   casos sinteticos. No se ha simulado cada combinacion de ajustes e idiomas.

No se ha flasheado ningun equipo en esta ronda. No se han cambiado bootloader,
particiones, eFuses, tablas de modelos ni rutas de recuperacion. No se ha
validado hardware X3 o X4 Pro; la retirada de distribucion de X4 Pro sigue
vigente. Ninguno de estos resultados constituye garantia contra brick.

## Organizacion de los menus

Se han corregido las discrepancias de comportamiento, no aplicado aun el
rediseno de seis grupos propuesto en `reader-menus-audit-2026-10-08.md`.
Siguen coexistiendo ajustes rapidos y el editor con vista previa, por lo que
queda trabajo de simplificacion de navegacion sin perder funciones.

Las instrucciones reproducibles estan en `agent-docs/simulator.md`,
`test/simulator/reader_settings.py` y `test/simulator/list_navigation.py`.
Las capturas, logs y resultados locales estan en `artifacts/upstream-sync/`;
el analisis estatico se conserva en `ux-audit-cppcheck.log` y la compilacion
limpia de desarrollo en `ux-audit-default-clean-build.log` dentro de ese
directorio. La de produccion se registra en `ux-audit-release-clean-build.log`.
