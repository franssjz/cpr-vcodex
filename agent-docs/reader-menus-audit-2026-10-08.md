# Análisis de los menús de lectura de CPR vCodex

Fecha: 8 de octubre de 2026. Alcance: árbol de trabajo actual del fork, con la integración de Crosspoint pendiente de commit. Este informe analiza la navegación, las opciones y sus efectos; no cambia el firmware.

Seguimiento: los fallos funcionales de este diagnóstico se han abordado en la
ronda documentada en `experience-debugging-2026-10-08.md`. La reorganización
del menú propuesta más abajo sigue pendiente; este informe conserva el
diagnóstico original.

## Conclusión

El problema no es solo que haya demasiadas opciones. Conviven el menú y los ajustes rápidos del fork con el editor de texto y la barra de herramientas de upstream, sin una unificación completa de nombres, controles y comportamiento.

En el X4, un EPUB sin notas al pie presenta **19 opciones de primer nivel**. La primera abre otros **18 ajustes rápidos**, mientras que la última abre un segundo editor de texto con vista previa. **Nueve ajustes se editan en ambos**, más un décimo caso, Bionic Reading / Focus Reading, que comparte almacenamiento pero no representa todos los modos de la misma manera.

Hay problemas funcionales que conviene corregir antes de reorganizar nada: una acción llamada guardar puede eliminar una marca en otra interfaz, el control binario de lectura enfocada no representa correctamente el modo Sutil del fork y la frecuencia de refresco omite un valor que sí existe en Ajustes globales. La fuente integrada también tiene un nombre incorrecto en las pantallas nuevas.

La propuesta es conservar las funciones del fork, pero tener **un único editor de texto, un único comportamiento por acción y seis grupos claros en el menú**. Cambiar solo el orden o activar la barra no resolvería las discrepancias.

## Hallazgos prioritarios

### Alta prioridad Guardar marca puede eliminarla

La misma etiqueta, `Save page mark` / `Guardar marca de página`, hace cosas distintas:

- En el menú de lista llama a `saveCurrentPageBookmark()`. Si la marca existe, informa de que ya está guardada y la conserva.
- En Barra > Más llama a `toggleCurrentPageBookmark()`. Si existe, la elimina.

**Reproducción:** con un EPUB sintético y dos SD independientes, activar dos veces esa opción dejó una marca en la lista y cero en la barra. Se comprobó el contador del archivo `bookmarks.bin`, no solo el mensaje visual.

Esto es una discrepancia funcional con pérdida de una marca, no una preferencia estética. La operación de alternar sí es razonable para un atajo, pero no bajo una etiqueta que promete guardar.

**Recomendación:** usar la misma acción en ambas interfaces. Si se quiere alternar, mostrar `Marcar esta página` o `Quitar marca de esta página` según el estado.

Referencias: [guardar sin eliminar](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:1234), [ruta de la lista](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:1563), [ruta de la barra](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:3501), [alternar marca](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:650).

### Prioridad media Bionic Reading y Focus Reading no son independientes

`focusReadingEnabled` es una referencia al mismo campo que `bionicReading`:

| Ruta | Nombre visible | Control y valores |
| --- | --- | --- |
| Ajustes rápidos y Ajustes globales | Bionic Reading | Apagado, Normal, Sutil |
| Ajustes de texto > Estilo | Focus Reading / Lectura enfocada | Casilla activada o desactivada |
| Barra > Texto | Focus Reading | Casilla activada o desactivada |

Con Sutil seleccionado, las casillas aparecen activadas, sin distinguir Sutil de Normal. Al desactivar y volver a activar, se vuelve a Normal; no se puede recuperar Sutil desde esas casillas. En el simulador se confirmó que entrar con valor `2` y pulsar la casilla guarda `0`; el código solo puede volver a `1`.

Además, la vista previa trata cualquier valor distinto de cero como lectura enfocada normal. El motor del libro distingue Normal de Sutil y solo activa la transformación de composición para Normal. Por tanto, **la muestra no representa fielmente Sutil**.

**Recomendación:** una única etiqueta y un selector de tres estados en todas las rutas, conservando el modo Sutil. La vista previa debe utilizar el mismo modo que la página real.

Referencias: [alias compartido](Z:/Dev/Personal/xteink/cpr-vcodex/src/CrossPointSettings.h:623), [control de tres estados](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/ReaderQuickSettingsActivity.cpp:61), [control binario](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/TextSettingsActivity.cpp:483), [vista previa](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/TextSettingsPreview.cpp:40), [configuración del motor](Z:/Dev/Personal/xteink/cpr-vcodex/src/CrossPointSettings.cpp:391).

### Prioridad media La misma fuente se llama Bookerly y Noto Serif

El selector del fork muestra **Bookerly**, mientras que Ajustes de texto y Barra > Texto muestran **Noto Serif** para el mismo valor `fontFamily = 0`. No son dos fuentes entre las que se esté cambiando: `BOOKERLY` y `NOTOSERIF` son alias del mismo índice, y el fork carga los recursos Bookerly.

La captura del editor con vista previa llega a indicar `Noto Serif, 18 pt` mientras utiliza la fuente de ese índice Bookerly. Esto explica la impresión de que los dos selectores hacen cosas distintas, aunque ambos modifican la misma preferencia.

**Recomendación:** obtener nombre, identidad y tamaños desde una única definición de fuentes. Mantener los índices persistidos para no cambiar la fuente elegida en las SD existentes.

Referencias: [selector del fork](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/FontSelectionActivity.cpp:61), [editor con vista previa](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/TextSettingsActivity.cpp:83), [alias de índices](Z:/Dev/Personal/xteink/cpr-vcodex/src/CrossPointSettings.h:133), [fuente que carga el motor](Z:/Dev/Personal/xteink/cpr-vcodex/src/CrossPointSettings.cpp:526), [nombre en la barra](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:2801).

### Prioridad media Nunca se muestra como 30 páginas

En Ajustes globales > Pantalla, Frecuencia de refresco ofrece seis valores: 1, 5, 10, 15, 30 páginas y Nunca. Ajustes rápidos solo declara los cinco primeros.

Cuando el valor guardado es Nunca, la función de presentación de Ajustes rápidos limita el índice a la última etiqueta disponible y muestra **30 páginas**. No cambia el valor por entrar, pero informa de otro valor. Si se pulsa Confirmar, el cálculo cíclico pasa a **5 páginas**, no a Nunca ni a la primera opción.

La etiqueta incorrecta se reprodujo en el simulador: la captura muestra 30 páginas y el JSON conserva `refreshFrequency: 5`, el valor de Nunca. La transición a 5 páginas se deduce del cálculo del controlador.

**Recomendación:** compartir el catálogo completo de valores entre todos los accesos. Si un valor no es compatible con cierto contexto, no presentarlo como otro valor válido.

Referencias: [seis opciones globales](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/SettingsActivity.cpp:182), [cinco opciones y recorte de etiqueta en Rápidos](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/ReaderQuickSettingsActivity.cpp:23), [avance cíclico](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/ReaderQuickSettingsActivity.cpp:219).

### Prioridad media Demasiadas entradas y ninguna jerarquía común

El menú mezcla navegación, anotaciones, diccionario, presentación, automatización, sincronización y mantenimiento. `Select Chapter` e `Ir a %` están separados; las acciones relacionadas con diccionarios ocupan tres filas; `Ajustes de texto` está al final, incluso después de `Borrar caché`.

Los ajustes llamados rápidos también mezclan tipografía, pantalla y parámetros avanzados. Cada pulsación en muchos de ellos cambia directamente el valor y lo guarda. No funcionan como una lista de categorías ni como el editor con selectores y vista previa.

La barra mejora la separación inicial en Índice / Texto / Más, pero **Más reproduce todo el menú salvo Seleccionar capítulo**: conserva Ajustes rápidos y Ajustes de texto, además de los controles repetidos. No resuelve por sí sola la acumulación.

Referencias: [orden del menú](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderMenuActivity.cpp:40), [18 ajustes rápidos](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/ReaderQuickSettingsActivity.cpp:40), [construcción de Más](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:3385).

### Prioridad media Los nombres no explican el alcance ni la interacción

- `Night Mode` / `Modo nocturno` y `Dark Mode (Experimental)` / `Modo oscuro (experimental)` modifican el mismo campo. El segundo nombre sugiere una función diferente o menos estable.
- `Alignment` y `Paragraph Alignment` modifican la misma alineación.
- `Ver subrayados` también muestra marcas de página. Una persona que guardó una marca puede no reconocer esa opción como su destino.
- `Fuente` / `Tipografía` abre pantallas distintas según la ruta. Una cierra al elegir y la otra permanece abierta para comparar la muestra.
- Los ajustes de texto son globales: se escriben en `SETTINGS` y se guardan en la configuración del dispositivo. Abrirlos dentro de un libro no los convierte en preferencias exclusivas de ese libro.
- La opción de sangría del editor usa `STR_PARAGRAPH_INDENTATION`, que no tiene entrada en `spanish.yaml`; queda pendiente esa traducción. No implica que toda la pantalla carezca de traducción.

Referencias: [etiquetas españolas](Z:/Dev/Personal/xteink/cpr-vcodex/lib/I18n/translations/spanish.yaml:93), [contenido de marcas y subrayados](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/BookmarksActivity.cpp:20), [guardado desde ajustes rápidos](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/ReaderQuickSettingsActivity.cpp:173), [guardado desde el editor](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/TextSettingsActivity.cpp:341), [sangría](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/TextSettingsActivity.cpp:390).

## Inventario actual

### Menú principal de lista

Orden para un X4 y un EPUB sin notas al pie. Con notas en la página actual aparece una entrada adicional después de Seleccionar capítulo. La opción Luz solo se añade cuando el hardware la ofrece; no corresponde al X4 sin luz.

| Posición | Opción | Qué hace |
| --- | --- | --- |
| 1 | Reading Quick Settings | Abre la lista de 18 ajustes del fork |
| 2 | Select Chapter | Abre el índice de capítulos |
| 3 | Look up word | Selecciona una palabra de la página para consultarla |
| 4 | Lookup history | Abre el historial de consultas |
| 5 | Dictionary | Abre la gestión y selección de diccionarios |
| 6 | View highlights | Lista subrayados y marcas del libro |
| 7 | Save page mark | Guarda la posición de la página, sin duplicarla |
| 8 | Highlight text | Selecciona texto para crear un subrayado |
| 9 | Night Mode | Alterna el modo oscuro |
| 10 | Reading Orientation | Abre el selector de orientación |
| 11 | Auto Turn | Configura páginas por minuto |
| 12 | Go to % | Salta a un porcentaje del libro |
| 13 | Take screenshot | Solicita una captura de la página |
| 14 | Show page as QR | Abre la presentación de texto mediante QR |
| 15 | Mark as finished | Solicita confirmación para marcar el libro terminado |
| 16 | Go Home | Sale del lector; puede intervenir la sincronización automática configurada |
| 17 | Sync Progress | Sincroniza progreso con KOReader, no fecha/hora |
| 18 | Delete cache | Limpia la caché del libro y sale del lector |
| 19 | Text Settings | Abre Fuente / Tamaño / Diseño / Estilo con muestra |

`Borrar caché` no es borrar el EPUB, pero es una operación de mantenimiento con cambio de pantalla. No debería compartir protagonismo con elegir capítulo o cambiar tamaño. La finalización sí tiene confirmación; no debe describirse como una operación inmediata sin protección.

### Ajustes rápidos

Sus 18 filas, en el orden actual:

1. Modo oscuro.
2. Frecuencia de refresco.
3. Corrección de desvanecimiento al sol.
4. Familia tipográfica.
5. Tamaño de fuente.
6. Interlineado.
7. Margen.
8. Alineación de párrafo.
9. Estilo integrado del libro.
10. División de palabras.
11. Bionic Reading.
12. Orientación.
13. Espaciado adicional entre párrafos.
14. Forzar sangría de párrafos.
15. Suavizado de texto.
16. Oscuridad del texto.
17. Modo de refresco del lector.
18. Tratamiento de imágenes.

Las opciones enumeradas y los valores numéricos avanzan cíclicamente con Confirmar: se puede pasar del máximo al mínimo sin desplegar las alternativas. La familia tipográfica abre otro selector; el tamaño también puede cambiarse desde la pestaña Tamaño de ese selector. Esto crea incluso dos accesos al tamaño dentro del recorrido de ajustes rápidos.

### Editor de texto con vista previa

Tiene 13 controles repartidos en cuatro pestañas:

| Pestaña | Controles |
| --- | --- |
| Fuente | Familia integrada o de la SD |
| Tamaño | Tamaños disponibles para la familia activa |
| Diseño | Interlineado, espaciado entre palabras, espaciado entre caracteres, separación entre párrafos, sangría, alineación y margen |
| Estilo | Lectura enfocada, división de palabras, estilo integrado y suavizado |

Es la mejor base para el editor unificado, pero **todavía no sustituye completamente a los ajustes del fork**: le faltan Forzar sangría, el selector completo de Bionic Reading, oscuridad de texto y tratamiento de imágenes. Los ajustes de refresco y pantalla deben tener su propio grupo, no añadirse todos a esta pantalla.

La muestra utiliza texto de ejemplo traducido, no el párrafo actual del libro. La barra, en cambio, repagina la página real al cambiar sus controles. El editor indica que ciertas opciones de Estilo no se muestran en la vista previa; eso es distinto de una vista previa fiel de todo el EPUB y su CSS.

### Barra y ajustes globales

La barra tiene tres herramientas: Índice, Texto y Más. Texto presenta cinco entradas: fuente, tamaño, interlineado, alineación y lectura enfocada. Su opción Fuente abre el editor completo con vista previa, no el selector del fork. Más contiene 18 opciones en el caso base del X4, porque solo retira Seleccionar capítulo del menú de 19.

La pestaña global de Lectura contiene 22 entradas, incluyendo el acceso a Ajustes de texto y numerosos ajustes individuales que ese editor ya contiene. Por tanto, la duplicación también reaparece fuera del libro.

Referencias: [cinco controles de la barra](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:2773), [apertura del editor desde la barra](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:3191), [ajustes globales](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/settings/SettingsActivity.cpp:206).

## Matriz de duplicaciones

| Función | Rutas actuales | Diagnóstico |
| --- | --- | --- |
| Familia tipográfica | Rápidos, su selector, editor con muestra, Barra > Texto, Ajustes globales | Misma preferencia; dos selectores y nombre incorrecto en uno |
| Tamaño | Fila de Rápidos, pestaña del selector de fuentes, editor, barra, Ajustes globales | Mismo valor; ciclo directo o selección explícita según ruta |
| Interlineado | Rápidos, editor, barra, Ajustes globales | Duplicado exacto |
| Margen | Rápidos, editor, Ajustes globales | Mismo rango 5 a 40, paso 5; distinta interacción |
| Alineación | Rápidos, editor, barra, Ajustes globales | Mismo campo y cinco opciones, nombres distintos |
| Estilo integrado | Rápidos, editor, Ajustes globales | Duplicado exacto |
| División de palabras | Rápidos, editor, Ajustes globales | Duplicado exacto |
| Espacio entre párrafos | Rápidos, editor, Ajustes globales | Duplicado exacto |
| Suavizado | Rápidos, editor, Ajustes globales | Duplicado exacto; muestra limitada |
| Bionic / Focus | Rápidos, editor, barra, Ajustes globales | Mismo almacenamiento, dominio de valores incompatible en la UI |
| Modo oscuro / nocturno | Menú, Rápidos, Más y ajustes globales de pantalla | Mismo efecto, nombres contradictorios |
| Orientación | Menú, Rápidos, Más y Ajustes globales | Mismo ajuste; aplicación visual y controles diferentes |
| Frecuencia de refresco | Rápidos y Ajustes globales de pantalla | Mismo campo, pero Nunca falta en Rápidos y se etiqueta como 30 páginas |

Duplicar un acceso puede ser útil si es un atajo al **mismo editor y comportamiento**. Aquí el problema es mantener implementaciones paralelas: nombres, dominios de valores, guardado y actualización visual pueden divergir.

## Parecidos que no conviene eliminar

| Controles | Diferencia real y tratamiento propuesto |
| --- | --- |
| Frecuencia de refresco y Modo de refresco | La frecuencia marca el ciclo periódico; el modo puede forzar el tipo de refresco. En la ruta común, el modo forzado tiene prioridad sobre el ciclo. Agruparlos y hacer visible esa dependencia |
| Modo oscuro, oscuridad del texto y suavizado | Invertir pantalla, modificar el tratamiento de oscuridad y usar grises en bordes son funciones diferentes. No fusionarlas como si fueran tres nombres de lo mismo |
| Sangría y Forzar sangría | La primera fija su tamaño; la segunda interviene en cuándo se aplica y en la prioridad frente al estilo del libro y el espaciado extra. Mantener ambos juntos |
| Buscar palabra, historial y diccionario | Consultar contenido, recuperar consultas y elegir recursos son tres tareas distintas. Agrupar, no borrar |
| Marcar página y subrayar | Guardar una posición y guardar un rango de texto son acciones diferentes. La lista compartida debería llamarse Marcas y subrayados |
| Sincronizar progreso y Sync Day | KOReader sincroniza la posición; Sync Day sincroniza fecha/hora. Conservar nombres que expliciten el objeto de la sincronización |

Con separación extra entre párrafos activada y Forzar sangría desactivado, la sangría positiva puede quedar suprimida por diseño. Un usuario que cambia el tamaño de sangría desde el editor, sin ver el interruptor que está en otro menú, puede concluir que el ajuste no funciona.

Referencias: [prioridad del refresco](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/ReaderUtils.h:233), [reglas de sangría](Z:/Dev/Personal/xteink/cpr-vcodex/lib/Epub/Epub/ParsedText.cpp:720).

## Organización propuesta

Un menú de seis grupos para los dos estilos de presentación, conservando las acciones existentes:

| Grupo | Contenido |
| --- | --- |
| Texto y composición | Editor único con Fuente, Tamaño, Diseño y Estilo; mismo editor desde Ajustes globales |
| Ir a | Índice, porcentaje y notas al pie cuando estén disponibles |
| Marcas y subrayados | Marcar o quitar marca de esta página, subrayar texto y ver elementos guardados |
| Diccionario | Buscar palabra, historial y gestionar diccionarios |
| Pantalla | Modo oscuro, orientación, refresco automático o forzado, frecuencia y corrección al sol; luz solo cuando exista |
| Más | Paso automático, sincronizar progreso, marcar terminado, captura, QR, mantenimiento de caché y salir del lector |

El editor de texto debe absorber oscuridad de texto, imágenes, Forzar sangría y los tres estados de Bionic Reading donde corresponda. El gestor de fuentes puede seguir siendo una herramienta separada para instalar, descargar o eliminar recursos; eso no equivale a elegir la fuente de lectura.

Los ajustes rápidos dejarían de existir como segunda lista de 18 controles. Si se desea conservar un acceso especialmente rápido, podría limitarse a unos pocos atajos que abran el editor único en la pestaña correspondiente, sin mantener otra lógica de cambio.

La barra puede seguir ofreciendo accesos directos a Índice y Texto, pero su catálogo de acciones debe derivarse de los mismos grupos. No basta con trasladar todas las filas actuales a Más.

Esta propuesta reduce la carga inicial, a cambio de un nivel adicional para algunas acciones. El atajo físico de marca puede seguir disponible; su función de alternar debe ser explícita y no confundirse con guardar.

## Aplicación y regreso al libro

Ambos editores guardan preferencias globales durante la edición, pero difieren en la experiencia:

| Ruta | Al confirmar | Cuándo se ve el resultado |
| --- | --- | --- |
| Ajustes rápidos | Cambia y guarda inmediatamente; familia abre un selector que cierra al elegir | La composición del libro se actualiza al salir de Rápidos; algunos ajustes de pantalla afectan antes al menú |
| Ajustes de texto | Selecciona o abre un picker; permanece en el editor y guarda cada cambio | Muestra sintética inmediata; libro al regresar |
| Barra > Texto | Selector o casilla, con guardado y repaginación | Página real bajo el panel; Fuente abre el editor completo |
| Orientación del menú de lista | Rota visualmente el menú y guarda un valor pendiente | Se aplica al lector al cerrar el menú, incluso al salir con Atrás |

Atrás no es Cancelar todos los cambios. Esta diferencia debe respetarse o diseñarse expresamente; cambiarla al reorganizar introduciría otra sorpresa.

Referencias: [comparación y aplicación de ajustes](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:1331), [regreso del menú](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:699), [aplicación en la barra](Z:/Dev/Personal/xteink/cpr-vcodex/src/activities/reader/EpubReaderActivity.cpp:3373).

## Origen y alcance

La comparación con `HEAD` anterior a esta integración muestra que el fork ya tenía Ajustes rápidos y un menú largo. El commit integrado de upstream, `28971493`, incorpora el editor de texto con vista previa. En la integración se conservaron ambos accesos: Ajustes rápidos al principio y Ajustes de texto al final. La sobrecarga no nació entera en la actualización, pero la convivencia de editores y los alias de fuente y lectura enfocada sí necesitan reconciliación.

La barra tampoco elimina el problema porque Más reutiliza la lista combinada. La solución debe hacerse en el catálogo y los editores compartidos, no únicamente en el diseño visual del menú.

Este mapa corresponde al lector EPUB. XTC usa un recorrido distinto que abre selección de capítulos; el lector TXT directo no abre este mismo menú completo en su bucle de entrada. No se debe aplicar automáticamente toda la propuesta a formatos sin texto recomponible ni afirmar que las rutas ya son iguales.

## Validación y próximos pasos

Se revisaron código, etiquetas de inglés y español, rutas de guardado y aplicación, y los menús del fork anterior y del commit upstream integrado. Se ejecutaron seis recorridos con el simulador X4 normal, sin parches de firmware ni tarjetas reales: ajustes rápidos y fuente, editor con vista previa, barra Texto, dos recorridos de guardado de marcas y frecuencia de refresco Nunca.

Evidencias locales:

- [Menú de lectura actual](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-njwfiu64/quick-font/menu.bmp).
- [Los 18 ajustes rápidos y Bionic en Sutil](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-njwfiu64/quick-font/quick-settings.bmp).
- [Selector que dice Bookerly](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-njwfiu64/quick-font/font-picker.bmp).
- [Editor que dice Noto Serif](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-njwfiu64/text-preview/font-preview.bmp).
- [Lectura enfocada con Sutil representado como casilla](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-njwfiu64/text-preview/style-subtle.bmp).
- [La barra aún dice guardar antes de eliminar la marca](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-hanxly06/marks-toolbar/second-save.bmp).
- [Nunca aparece incorrectamente como 30 páginas](Z:/Dev/Personal/xteink/cpr-vcodex/artifacts/upstream-sync/reader-audit-bs7sf9hf/refresh-never/quick-settings.bmp).

Las capturas son muestras parciales de listas con scroll; no muestran simultáneamente todas sus opciones. Los archivos de prueba permanecen en `artifacts/upstream-sync/`, fuera del firmware. No hubo flasheo ni validación física en este análisis.

Orden recomendado para una futura implementación:

1. Corregir Guardar marca, el nombre Bookerly, la representación completa de Bionic Reading y el valor Nunca de refresco.
2. Completar el editor con vista previa para que preserve todas las funciones tipográficas del fork.
3. Hacer que accesos rápidos y Ajustes globales reutilicen ese editor, sin cambiar el alcance global de los ajustes ni sus claves persistidas.
4. Reorganizar las acciones en grupos compartidos por lista y barra; unificar etiquetas y completar traducciones.
5. Validar que cada ruta produce el mismo valor guardado y el mismo resultado: fuentes integradas y SD, Normal/Sutil, marcas existentes, sangrías y refresco. Comprobar también conservación de posición al repaginar, regreso con Atrás y cambio de orientación.
6. Probar físicamente en X4 tras la validación en simulador. Esta reorganización no necesita cambiar particiones, bootloader ni compatibilidad de hardware.

**Decisión recomendada:** unificar primero la semántica y los editores; reorganizar después. Ocultar duplicados sin trasladar las funciones que solo ofrece el fork podría hacer perder opciones, aunque visualmente el menú quedase más limpio.
