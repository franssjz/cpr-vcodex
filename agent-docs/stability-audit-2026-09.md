# Auditoría de estabilidad X3/X4 — 15 de septiembre de 2026

## Alcance y criterio

Referencia inicial: `08180f99` en `master`, sin modificaciones locales.
Incidencias: [#219](https://github.com/franssjz/cpr-vcodex/issues/219),
[#217](https://github.com/franssjz/cpr-vcodex/issues/217) y el reporte del
mantenedor sobre idioma/OTA en .33. X4 Pro sigue retirado. X3 y X4 pueden
necesitar OTA o actualización desde SD: el modelo no garantiza recuperación USB.

No se publican cambios por el mero hecho de compilar. Conservar la partición
activa, el bootloader, la tabla de particiones y los datos del usuario es parte
de la validación. Nunca cambiar eFuses para realizar estas pruebas.

## Historia de las regresiones

| Problema | Primera versión demostrada | Evidencia y alcance |
| --- | --- | --- |
| OTA detecta versión y falla antes del primer byte | 1.6.0.31 | #219: X3 .31 → .32; comentario: X4 .32 → .33. El cambio .30 → .31 sustituye el transporte anterior por `esp_http_client` con certificados en C3. El fallo de reserva TLS es una explicación de código pendiente de confirmar con un log del fallo físico. |
| Idioma cambia tras actualizar | 1.6.0.31 | El generador pasa de `_order` a orden BCP47, pero `I18n::loadSettings()` sigue convirtiendo directamente el número de `language.bin` al enum nuevo. `main.cpp` ignora el código estable de JSON. |
| Nombre de hebreo ilegible en el selector | Reproducido en HEAD inicial | El selector fuerza Noto Sans de lectura, que no incluye hebreo. Las fuentes Ubuntu UI ya incorporadas cubren los 24 nombres, incluido vietnamita. Se corrigió la selección de fuente sin añadir datos de fuentes. |
| Abortos por memoria en imágenes | Presente al menos en 1.5.0.9 | `BitmapHelpers.h` utiliza `new[]` sin `nothrow` para filas del difusor; PNG también lo hace para objetos y acumuladores. Con excepciones desactivadas, la falta de memoria aborta. Es un defecto confirmado del código; no demuestra que todos los síntomas de #217 tengan esta causa. |
| Pantalla de suspensión retenida, bloqueos y latencia durante lectura | Reportado en 1.6.0.32 | El usuario saltó desde una versión de principios de agosto sin identificarla. No hay una versión inicial exacta ni backtrace. .33 ya introdujo correcciones de memoria, imágenes, entradas y refresco; esto no equivale a demostrar que la incidencia esté resuelta. |
| BIN de depuración demasiado grande pese a build verde | Reproducido en esta auditoría | El perfil DEBUG produjo 6.554.640 bytes: 1.040 más que el slot X4. PlatformIO informa tamaño ELF, que no incluye todo el relleno del BIN. Se añadió el límite al empaquetado y se redujo el nivel de logs de `default` a INFO. |
| README anuncia una release inexistente | HEAD inicial | GitHub tenía .33 publicada; la API de .34 devolvía 404. README anunciaba .34 y enlazaba su descarga. Corregido: separar estado publicado de reparaciones pendientes. |

La fecha del merge grande que entró en .31 es 2026-09-11 (`679ad904`,
integración de `c3673500`/`233f93ff`). La documentación previa mencionaba
2026-09-05 para la integración en la rama de trabajo: no confundir ambas fechas.

### Idiomas: datos y límites de recuperación

Orden de .24 y .30:

```text
EN ES FR DE CS PT RU SV RO CA UK BE IT PL FI DA NL TR KK HU LT SI VI
```

Orden de .31–.33:

```text
EN BE CA CS DA DE ES FI FR HE HU IT KK LT NL PL PT RO RU SI SV TR UK VI
```

El BIN publicado .33 mide 6.391.952 bytes y tiene SHA-256
`2746e493e84c3f350c09cec43ce11f5f1267ed60af7bdd94b9d73db99466e098`.
La copia local coincide con el digest de GitHub. Se inspeccionó su tabla de
nombres: incluye los 24 idiomas, incluido español. No se ha reproducido la
ausencia visual del español ni el salto concreto a ruso en ese dispositivo.
En concreto, el ordinal antiguo de español (1) pasa a bielorruso (BE) con
el enum nuevo, que también usa cirílico. Esto explica un cambio de escritura,
pero no permite identificar con certeza el idioma que vio quien reportó ruso.

La corrección usa `settings.json.language` cuando existe, como CrossPoint y
CrossInk. Solo si falta migra los 23 índices históricos. No sobrescribe
`language.bin`, y no guarda un EN predeterminado durante una migración previa
de ajustes. Archivos truncados o índices inválidos no se interpretan.

Limitación: .31–.34 pudieron sobrescribir un ordinal sin cambiar la versión
del archivo, y guardar EN en JSON al cambiar otros ajustes. Si ambos registros
ya perdieron la intención original, no es posible inferirla con certeza. Se
respeta el código JSON explícito; el usuario podrá elegir de nuevo su idioma.
No se debe aplicar una conversión especulativa del tipo «ruso significa español».

## Correcciones y pruebas

- Idioma: una fuente persistente basada en códigos, migración histórica,
  comprobación de lectura completa y pruebas de todos los índices, códigos,
  nombres del menú y traducción española. No sobrescribir ajustes existentes
  si su lectura falla. Cobertura de los 24 nombres en las fuentes del selector.
- OTA: manifiesto completo, tamaño entero positivo de 32 bits, URL HTTPS
  no truncada y SHA válido. El tamaño mínimo se valida antes de `esp_ota_begin`.
- Pruebas del `OtaUpdater.cpp` real con transporte/flash simulados: cero bytes,
  interrupción, longitud corta/larga, digest distinto, chip S3, etiqueta X4 Pro
  dividida entre fragmentos, error de escritura/finalización/selección y downgrade.
  Verifican que los fallos no seleccionan el candidato. La criptografía y el
  driver ESP se sustituyen en estas pruebas: no son validación de hardware.
- Memoria: reservas fallables y comprobación de todas las filas de dithering
  en BMP/JPEG/PNG; PNG usa propiedad automática para objetos y acumuladores.
  Se mantienen umbrales, formatos de caché y tamaño de cada reserva.
- Empaquetado: límite aplicado al BIN completo en todos los perfiles de
  firmware. Pruebas en el límite exacto y un byte por encima. Compilar deja
  de actualizar el README como si el candidato ya estuviera publicado; esa
  actualización corresponde al sincronizador de releases publicadas.
  Se rechazan chip/descriptor inválidos y versiones internas distintas a los
  metadatos. El empaquetado se ejecuta también al recuperar el BIN de caché:
  el post-action anterior se omitía en ese caso y podía dejar artefactos viejos.
- Flash: conversión sin pérdidas de las 24 tablas de kerning aún densas,
  siguiendo el formato sparse ya soportado por el lector. Se conservan todos
  los valores, mapas de clases, glifos, métricas y ligaduras. Ahorro de datos:
  333.761 bytes. No se cambia el formato de fuentes SD ni se quitan fuentes.
  Herramienta reproducible: `lib/EpdFont/scripts/compact_builtin_kerning.py`.
  Los hashes originales proceden de `08180f99`; las pruebas comprueban cada
  celda y el resto del encabezado. Otra prueba llama al `EpdFont::getKerning`
  real para cada pareja de clases de las 24 fuentes contra los valores previos.
- CI: registra también las pruebas de manifiesto Auto Flash, idioma, cobertura
  de fuentes y OTA; configura el preprocesador C++20 de MSVC. El generador de
  idiomas usa la biblioteca estándar de Python, sin dependencias nuevas.

Los logs y BIN de trabajo se guardan en `artifacts/stability-*`, ignorados por
Git. No son assets de release ni se copian a Auto Flash.

Resultado nativo completo en WSL/GCC 10: **269/269 pruebas CTest correctas**,
incluidas las pruebas Python de empaquetado. En Windows/MSVC también se
compilaron y ejecutaron las suites específicas de lectura, idioma y OTA.

El candidato local `gh_release` `1.6.0.36-cpr-vcodex.bin` mide **6.102.496
bytes**, deja **451.104 bytes libres** en el slot X4 de 6.553.600 y pasa
checksum/digest de imagen de `esptool image-info`. Es un número de compilación
local, no una publicación. El ELF ocupa 6.088.563 bytes (92,90 %), con RAM
estática de 58.244 bytes, sin incremento. **Pasa el presupuesto original del
97,5 %**, con 301.197 bytes de margen frente a ese presupuesto. En la primera
tanda lo excedía en 32.499 bytes; el límite no se ha relajado.
SHA-256 del candidato actualizado:
`5e674227b5c4b552402e7e2fcff416af2d6664475b40537efc74776bb0d50711`.

El perfil `default` con INFO (`1.6.0.35.dev5-08180f99-cpr-vcodex.bin`)
mide 6.165.648 bytes (387.952 libres), ELF 6.151.725 y RAM 58.252, y pasa
la validación de imagen. El DEBUG rechazado se conserva con extensión
`.bin.rejected` en `artifacts/stability-rejected`, fuera del selector de BIN.

`src/version.cpp` sustituye el descriptor débil del framework por uno con la
misma versión generada que la UI. Se comprobaron builds incrementales .36 →
.37 → .36 y el perfil de desarrollo. Los límites eFuse, MMU, secure version y
campos reservados coinciden byte a byte con el descriptor previo. No se han
cambiado eFuses en hardware. El IDF se identifica por major/minor/patch del SDK.

Evidencia: `artifacts/stability-compact-{release,default}-build.log`,
`stability-compact-release-budget.{json,md}`, `stability-compact-validation.log`,
`stability-compact-results.json` y `stability-compact-linux-tests.log`.
La .36 fue un candidato local. El mantenedor informa de que la ha instalado
y solicita publicar la siguiente corrección como .37. El código, notas y
automatización de release se preparan para esa versión; las pruebas físicas
pendientes siguen siendo necesarias para cerrar las incidencias.

### Consulta OTA en .36 y preparación de .37

La consulta del manifiesto público .33 devuelve HTTP 200 y el parser real lo
acepta. Las pruebas con versión instalada .36 y publicada .33/.36 devuelven
una consulta correcta y prohíben instalar una versión anterior o igual. Por
tanto, estar por encima de la release no explica por sí solo el error mostrado
en el X4: falta un log físico de la consulta que falló.

La .37 muestra versión instalada/publicada y «Dispositivo actualizado» cuando
la consulta es correcta y no hay una versión superior. Distingue el fallo de
consulta del fallo de instalación, y reintenta el mismo manifiesto publicado
desde el host raw de GitHub si Pages falla. Ambos usan HTTPS con certificados;
si fallan ambos, se conserva el error y no se afirma que esté actualizado.
Se prueban la recuperación por el segundo host y el fallo de ambos sin escritura.
Los builds de desarrollo tampoco ofrecen una release numéricamente anterior.

La publicación incorpora una barrera de pruebas nativas en el workflow de
release, además del presupuesto de flash y la validación del BIN. Pages debe
sincronizarse desde el asset efectivamente publicado y comprobarse por SHA-256.

El simulador ejecuta la actividad OTA real con respuestas de red simuladas:
«Dispositivo actualizado» con ambas versiones y fallo de consulta en español.
Las dos pantallas caben en X4 y muestran Volver/Reintentar. Capturas locales en
`artifacts/release37-ota-ui`. Esto valida la presentación, no TLS en hardware.

### Pruebas del simulador X4

Se compiló en WSL/GCC 10 una copia aislada de las fuentes y del simulador
`boydj/crosspoint-simulator`, rama `cpr-vcodex`, commit `9d9eccd4`.
La copia necesitó dos adaptaciones: `HTTPClient::begin()` devuelve `bool` y
`HalGPIO::rawInputActive()` observa eventos SDL pendientes. Quedan en
`artifacts/stability-simulator-compat.patch`; no se editaron dependencias en
`.pio/libdeps` ni los repositorios de referencia.

- Seis arranques correctos: español histórico, inglés/ruso explícitos en JSON,
  instalación vacía, archivo antiguo truncado y JSON ilegible. Se comprobó el
  JSON resultante y se conservó byte a byte el archivo antiguo; el JSON dañado
  tampoco se sobrescribió.
- Navegación con botones hasta Ajustes → Sistema → Idioma. Reproducido y
  corregido el nombre hebreo ilegible; español, hebreo y vietnamita comprobados
  visualmente. Se alcanzó el final de la lista y se volvió al principio.
- Selección español → inglés → español y reinicio después de cada cambio:
  idioma conservado. Capturas `artifacts/stability-sim-reboot-*.bmp`.
- Apertura de `test/epubs/test_mixed_images.epub`, cambios de página y ciclo
  de suspensión/despertar simulado. Se restauró capítulo 3, página 0 (84 %).
  Las capturas `stability-sim-page.bmp` y `stability-sim-wake.bmp` son idénticas
  (SHA-256 `182a98ba91ffcc0955aa9b3dda5529781a4406da3d511eeb9b577d6a3dd90963`).
- Tras compactar las fuentes se repitió ese recorrido con los ejecutables
  anterior y nuevo sobre SD simuladas independientes. Las tres capturas
  (texto inicial, página con imagen y página tras despertar) son idénticas
  byte a byte. Resultados: `artifacts/stability-kerning-visual-ddolqzw_`.

El simulador usa `stb_image` para JPEG y métricas de heap sintéticas. El
despertar reinicia un proceso de escritorio. Estas pruebas validan navegación,
persistencia y restauración de página; no validan TLS/OTA, el decodificador
JPEG del ESP, escasez de RAM real ni la pantalla de tinta tras deep sleep.

Logs y recetas locales: `artifacts/stability-simulator-checks.py`,
`stability-sim-reading.sh`, `stability-simulator-build.log` y
`stability-sim-reading.log`. El X4 conectado no recibió ninguno de estos BIN.

## Comparación con los proyectos de referencia

| Referencia | Decisión |
| --- | --- |
| CrossPoint `233f93ff`; CrossInk `7a092e88` | Adoptar la persistencia del idioma por código, adaptando el arranque JSON y la migración propios del fork. |
| CrossPoint `c484dc72` (#3144), código presente en referencia local `aa994cf7`; CrossInk `7a092e88` | Ambos admiten kerning sparse. Completar las tablas del fork que seguían densas con una conversión exacta, sin regenerar/rasterizar glifos ni importar cambios generales de tipografía. |
| CrossPoint `3555ff55` (#2332), revisado mediante API el 15/09 | Adoptar comprobaciones de falta de memoria en imágenes. No importar toda la agrupación de buffers ni los cambios XTC: exigirían bloques contiguos mayores o alterarían otros formatos. |
| CrossPoint `4a679d5d` (#3341) | El apagado SD antes de dormir y la política de despertar ya están presentes. No duplicarlos ni sustituir sin evidencia el SDK fijado en `cb9167d5`. |
| CrossPoint `3e627112`, corrección OTA previa `08180f99` | Conservar descarga al slot inactivo, adaptada al BIN C3 del fork, y cubrir los fallos de activación. Verificar físicamente TLS y el wrapper de revisión eFuse antes de release. |
| CrossInk 1.5.1 | Revisadas sus mejoras de cachés, cancelación de indexado, memoria de imágenes y manejo de ajustes por libro. La migración de persistencia y las optimizaciones que dependen de PSRAM requieren trabajo separado. |
| CrossPoint `d707f14b`, `581c6374`, `79a28ab3` | Revisados títulos/alcance: reserva de listas de ajustes, zona horaria/DST y pantalla About. No añadir funciones mientras quedan fallos de OTA/despertar sin validación física. |

### Decisión sobre volver atrás

No hacer un rollback global a .30: también contiene las reservas de imágenes
que abortan, y desharía soporte de paneles, reparaciones de cachés y migraciones.
.30 es una referencia para comparar OTA/UX y hacer pruebas controladas sobre
copias, no una versión declarada «sin errores». Mantener correcciones pequeñas
y comprobables sobre el árbol actual. No reescribir el historial de `master`.

## Validación física y cierre

El mantenedor dejó un X4 accesible por USB. Se confirmó ESP32-C3 rev. 0.4,
sin Secure Boot ni Flash Encryption, y se guardó una copia íntegra de 16 MiB.
`app0` empieza en 0x10000, `app1` en 0x650000, ambos de 0x640000 bytes.
Las entradas OTA válidas indican `app1` (.32) activo y `app0` (.30) anterior.
La copia de flash no incluye los archivos de la microSD.

SHA-256 de la copia inicial:
`753abeda464d34fe9fdc482aee3530d397b4332085b0985fd5023fd2f62dd636`.
Ruta local: `artifacts/stability-x4-backup/flash-before.bin`.

Pendiente para validar físicamente la release y cerrar #217/#219:

1. X4 recuperable: guardar ajustes y datos SD antes de pruebas que los cambien;
   verificar candidato, arranque y ausencia de pánico. Conservar el slot activo
   y la copia de recuperación.
2. Probar selección de español y otros idiomas, reinicio, cambios en otros
   ajustes, arranque desde .30 y desde .32/.33. Confirmar que todas las filas
   del selector son accesibles con botones.
3. Lectura prolongada con texto, imágenes JPEG/PNG/BMP, fuentes internas/SD y
   cambios de capítulo; recopilar heap libre, bloque máximo y backtrace si falla.
4. Suspensión manual y automática, alimentación USB/batería, varias pantallas
   de reposo, pulsación corta/larga y reanudación repetida.
5. OTA: validación del manifiesto, descarga completa, reinicio y segunda
   actualización; interrupción de red conservando el arranque anterior. Probar
   el puente File Transfer → SD Update desde una versión afectada.
6. Repetir en X3, incluidas variantes UC8253/UC8279d. El X4 no prueba su
   controlador, alimentación SD, RTC ni geometría.

No afirmar «cero errores» por pruebas nativas o por un solo arranque. Anotar
exactamente qué dispositivo, versión inicial, BIN y recorrido se comprobaron.

## Seguimiento físico de OTA — 16/09/2026

La .37 publicada seguía fallando al consultar actualizaciones desde Ajustes.
Se reprodujo en el X4 ESP32-C3 rev. 0.4 del mantenedor, verificando primero
por USB que `app1` contenía exactamente el BIN público de .37. Los diagnósticos
privados se cargaron en `app0`; no se modificaron bootloader, particiones ni eFuses.

- La conexión falló tanto con el transporte manual de .37 como con
  `esp_http_client_perform` configurado como en .30, con y sin comprobación
  del nombre del certificado. El error fue `PK verify failed 0x4290` seguido
  de `mbedtls_ssl_handshake -0x3000`. Los headers del SDK descomponen 0x4290
  en `MBEDTLS_ERR_RSA_PUBLIC_FAILED` + `MBEDTLS_ERR_MPI_ALLOC_FAILED`:
  falta memoria durante la verificación de la firma, no falta una release.
- Referencia aplicada: CrossInk `7a092e8822c9c90e8beecacd317acc13d3e24dfb`,
  `SettingsActivity::runAction` y `silentRestartToNetwork(OTA)` / arranque
  de red en `main.cpp`. Se adapta como `silentRestartToOta()` al token RTC
  existente del fork, consumido una sola vez, respetando arranque de recuperación
  y errores. OTA arranca sin la actividad Ajustes retenida ni fuentes SD de lectura.
  Se conservan idioma, stores JSON del fork, URLs, manifiesto, BIN y SHA-256.
- Con esa adaptación, el X4 pasó de unos 67 KB libres / 45 KB contiguos a
  88 KB libres / 73 KB contiguos antes de consultar. `checkForUpdate()` devolvió
  `OK`, con `1.6.0.37-cpr-vcodex` como versión publicada y certificado verificado.
- La descarga real completa devolvió `OK`: 6.103.328 bytes, SHA-256
  `4ce7d7da54dcfcbaec8ad76a33705c5f19425d774763027137788d9cb9fc0c56`,
  idéntico al manifiesto y al BIN público. Esta primera prueba no instaló el
  firmware descargado. Se restauraron y verificaron los selectores originales
  de .37 (secuencias 47/48).

Evidencia local: `artifacts/ota37-probe-serial.log` (fallo reproducido),
`artifacts/ota-network-boot-serial.log` (consulta y descarga correctas).
Los hooks de arranque automático y las capturas son privados y no forman
parte del firmware distribuible. X3 todavía requiere verificación física.

### Instalación OTA completa

Se ejecutó `OtaUpdateActivity::runUpdateInstall()` sin modificar su política
de validación ni el código de escritura, desde el diagnóstico privado
`1.6.0.37.dev4-fddda85c` con el arranque de red corregido hacia el BIN público
`1.6.0.37-cpr-vcodex`. El X4 descargó y escribió 6.103.328/6.103.328 bytes,
`installUpdate()` devolvió `OK` y la captura registró el reinicio posterior.
La verificación USB de `app1` contra el BIN público dio digest coincidente.
Después se restauraron y verificaron ambos selectores OTA originales.

El primer intento salió del selector Wi-Fi antes de consultar; no inició
ninguna instalación y no se cuenta como prueba satisfactoria. La repetición
con el mismo diagnóstico completó el recorrido. Evidencia:
`artifacts/ota-install-serial.log` y `ota-install-verify-installed.log`.

El candidato normal .38, sin hooks de diagnóstico, compila en `default` y
`gh_release`; supera las 269 pruebas nativas y el pre-release check. Su BIN
local tiene 6.103.744 bytes (449.856 libres en el slot X4) y SHA-256
`99262b0dc0433e193be7a8f7397db5bfbf3fb32bedd709f3e01f183967f61856`.
Esto valida un X4 concreto; no sustituye pruebas físicas de X3 ni de todas
las redes, tarjetas, bibliotecas y estados de memoria de los usuarios.

Al terminar, el X4 quedó con ese BIN normal .38 en `app0` (0x10000),
verificado por digest antes de seleccionarlo. `app1` conserva la .37 pública,
también verificada. El selector de .38 (secuencia 49) y la permanencia del
selector anterior (48) se comprobaron por lectura; el equipo volvió a aparecer
por USB. Logs locales: `artifacts/ota38-final-*.log`. La publicación de .38
y la sincronización de Pages quedan pendientes; estos resultados no convierten
un BIN local en un asset ya publicado.

### Instalación física de las correcciones de imágenes — 16/09/2026

Antes de instalar el candidato con #215 y JPEG progresivos #2925 se volvió
a leer la tabla y ambos selectores del X4 del mantenedor. La secuencia 49 de
la instalación anterior estaba en estado `ABORTED` (4); la secuencia 48 de
.37 seguía `VALID` (2). La comprobación anterior de aparición por USB no
demostraba que .38 hubiera quedado confirmada. No se ha determinado la causa
del aborto; interrumpir la primera inicialización para leer por esptool puede
provocar rollback, por lo que esta vez se dejó completar antes de reiniciar.

Se verificó por digest la .37 pública en `app1` y se escribió únicamente
`app0` con `1.6.0.38.dev3-fddda85c-cpr-vcodex.bin` (6.166.944 bytes;
SHA-256 `07501529ea4b84fd79227850cccae75712c46e6821c58688b457e86eb5fd1e97`).
Después de verificar el BIN en flash se activó su selector como `NEW` y
se dejó arrancar. La lectura posterior confirmó que el propio firmware lo
había pasado a `VALID` (2), con CRC correcto. La tabla y el sector completo
del selector de recuperación permanecen idénticos. No se escribió bootloader,
NVS ni la partición de recuperación.

Los dos arranques observados por serie detectaron X4 y SD y completaron los
refrescos de pantalla. Esto no verifica todavía la apariencia física de los
dos EPUB. Windows no expone la SD como volumen montado: los archivos del
escritorio no se copiaron al lector por USB. Evidencia local:
`artifacts/x4-images-*.log`, `x4-images-before-layout.bin` y
`x4-images-after-layout.bin`. Esta es una instalación de desarrollo local,
no una release publicada.
