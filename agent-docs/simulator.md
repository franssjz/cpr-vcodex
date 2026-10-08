# Desktop Simulator

The firmware runs on the desktop through
[crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator):
a native PlatformIO build that swaps `lib/hal/` for a host HAL and paints the
e-ink framebuffer in an SDL2 window. It is the fastest way to check screens,
touch layouts, list conversions, and web-server routes without a device. It
does not model e-ink refresh, panel controllers, heap limits, deep sleep, or
OTA (firmware updates are stubbed out).

## Setup (once)

1. `brew install sdl2` (Linux: `libsdl2-dev libssl-dev`).
2. `platformio.ini` fetches the simulator from the fork
   `boydj/crosspoint-simulator`, branch `cpr-vcodex`. The upstream simulator
   only knows upstream's HAL; that branch adds the fork's `HalClock` UTC API,
   `HalGPIO::coldBootImpliesPowerButton`, `HalTiltSensor::readGyro`, plus host
   shims for `<Esp.h>`, `<esp_heap_caps.h>`, SdFat `common/FsDateTime.h`,
   `MD5Builder::getBytes`, the `esp_http_client` calls in `lib/KOReaderSync`,
   and the Wi-Fi channel/BSSID overloads. To work on the simulator itself,
   clone it beside this repo and point the env at the checkout from the
   gitignored `platformio.local.ini`:

   ```ini
   [env:simulator]
   lib_deps =
     ${env:simulator.lib_deps}
     simulator=symlink://../crosspoint-simulator
   ```

   Merge `crosspoint-reader/crosspoint-simulator` `main` into that branch when
   pulling firmware updates; keep the fork's additions small and additive.
3. Put EPUBs in `fs_/books/` (gitignored). That directory is the simulated SD
   card root; `fs_/.crosspoint/` holds the simulated caches and settings.

## Build and run

### October 2026 SDK Compatibility

The full upstream sync needs newer HAL interfaces in addition to the fork's
UTC clock, Wi-Fi and storage shims. The validation snapshot uses upstream
simulator `097f44e08492d9dde40d45c1bce189fd1e782e0e` plus
`test/simulator/cpr-hal-097f44e.patch`. Apply that patch in a separate checkout
of that exact simulator revision, then use a `symlink://` dependency pointing
to the patched checkout. Do not patch `.pio/libdeps` or overwrite an existing
simulator checkout's changes. The patch has been checked with `git apply --check`.

The simulator env now includes SDK JsonSax, CatalogList, ContentProtection and
FreeInkFont dependencies. A local `lib_deps` override must retain those along
with FreeInkUI, Icons and its previous third-party dependencies. The native
content backend is not a validation of protected-book crypto or device TLS.

WSL Ubuntu 20.04's default GCC 9 cannot build the new C++20 code. Select GCC 10
or newer through PATH (temporary `gcc`/`g++` links work); native PlatformIO
replaces pre-script `CC`/`CXX` values while detecting its compiler.
For headless screenshots, use `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software`.
The HAL patch prevents firmware clock initialization from setting host time.

On this workspace the validated copy and scripts are under
`artifacts/upstream-sync/`, with the isolated WSL workspace recorded in
`simulator-workspace.txt`. Ten grayscale regression boots passed, covering
AA on/off, light/dark modes, decoded/cached images and text-only pages.
Spanish Lyra Custom and Lyra Carousel home captures also exercise a v1 recent
book migrating to v2 stable identity. The shared date/clock/battery layout is
covered by five native regression tests.

```bash
pio run -e simulator_x4_pro -t run_simulator   # X4 Pro: touch, Home key, frontlight
pio run -e simulator -t run_simulator          # X4 (buttons only)
pio run -e simulator_x3 -t run_simulator       # X3 landscape, tilt sensor
```

On Windows, run the simulator inside WSL (native Windows is not supported by
crosspoint-simulator). Install `libsdl2-dev` and `libssl-dev`, then use the GCC
profile:

```bash
pio run -e simulator_x4_pro_wsl -t run_simulator
```

Keys: arrows = side/front buttons, Return = Confirm, Escape = Back, P = Power,
S = sleep, H = Home key (X4 Pro), mouse = tap and swipe on touch profiles.

Simulator builds report the version `<base>.sim-<sha>` and do not touch the
release or dev counters (`scripts/git_branch.py`).

## Scripted checks and screenshots

The binary accepts a timed input script and screenshot schedule, which makes
screen reviews repeatable and needs no desktop automation permissions:

```bash
mkdir -p qa-artifacts
CROSSPOINT_SIM_INPUT_SCRIPT='2000:TAP:240,530;3200:SWIPE:400,10,400,300,300;4400:HOME:100;5400:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='2700:qa-artifacts/apps.bmp;3900:qa-artifacts/frontlight.bmp' \
  .pio/build/simulator_x4_pro/program
```

Actions: `BACK`, `ENTER`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `POWER`, `SLEEP`,
`HOME`, `QUIT`, `TAP:x,y[,hold]`, `SWIPE:x1,y1,x2,y2[,ms]`. Coordinates are
logical display pixels. Screenshots are BMP at the host's drawable resolution
(2x on Retina); convert with `sips -s format png`. `CROSSPOINT_SIM_FREE_HEAP`
and `CROSSPOINT_SIM_MAX_ALLOC_HEAP` fake low-memory conditions. The web server
binds `http://127.0.0.1:8080/` (`CROSSPOINT_SIM_HTTP_PORT` to move it).

## EPUB image grayscale regression

Run `python3 test/simulator/epub_image_grayscale.py /absolute/path/to/program`
with an X4 simulator binary. It generates a synthetic EPUB and isolated SDs,
then checks actual screenshot pixels across ten boots: text AA on/off, light
and dark modes, decoded and cached images, and a text-only page. It also checks
that disabling text AA keeps the paragraph monochrome without losing image
grays. The script reports its artifact directory; `--output-parent` can place
it under the gitignored `artifacts/`. It uses only the Python standard library.
This checks composition, not physical panel tones or refresh waveforms.

## Popup scrolling regression

`python3 test/simulator/popup_scroll.py /absolute/path/to/program` navigates
the actual X4 Settings > Sleep screen picker on a fresh simulated SD. It
checks all 13 choices, both wrap directions, and the saved last selection.
Use `--theme 0` (Lyra), `--theme 1` (Lyra Custom), or `--theme 3` (Classic). Captures
and logs are retained in the reported directory (`--output-parent` supported).

For long-message and long-label stress cases, use an **isolated simulator
checkout only**: apply `test/simulator/popup_preview.patch` with
`git apply --unidiff-zero`, rebuild, then run
`python3 test/simulator/popup_stress.py /absolute/path/to/program`.
The fixture includes `PopupPreviewActivity.h` and opens it only when
`CPR_POPUP_PREVIEW` is set. It exercises 40 wrapped options and a 35-line
message in all four orientations, asserting scroll/return screenshot equality
and exactly one callback with the correct selection. Remove the fixture with
`git apply -R --unidiff-zero test/simulator/popup_preview.patch` and rebuild
before reusing that simulator checkout normally. Never apply the fixture to a
checkout used for device firmware builds.

Native `PopupViewport.*` tests additionally cover pixel bounds, overflow,
selection visibility, oversized individual rows, resize, and scroll clamping.
These checks do not validate physical touch hardware or e-ink refresh quality.

## List navigation regression

In an **isolated simulator checkout only**, apply
`test/simulator/navigation_fixture.patch` with `git apply --unidiff-zero`
and rebuild. Run
`python3 test/simulator/list_navigation.py /absolute/path/to/program`
(`--output-parent` supported). The fixture uses `NavigationFixture.h` to open
real activities and temporarily logs logical selection, painted selection and
the measured viewport. Each scenario creates a disposable simulated SD.

The 47 scenarios check every available Apps destination, including the final
Plugins entry, four Sync Day actions, list wrapping/scrolling, tab focus, and
short-list height in Classic/Lyra with English/Spanish labels. Reader-menu
cases also traverse every row and change orientation while a lower row is
selected, checking that the final displayed viewport contains the selection. Network time
sync, real plugin services and physical inputs are not validated. The empty
test SD also does not exercise every book-dependent action.

The integration regression was an obsolete `hasSubtitle` boolean passed to
the new integer `selectionOffset` parameter: `true` shifted the painted row
back one while Confirm kept using the logical selection. `--baseline` runs
only the first case; the old code fails with `Apps: unexpected offset 1`.
The production header now rejects boolean offsets at compile time.

Native `ListNavigation.*` tests compile the production viewport/height methods
against the actual SDK renderer and input buffer. They check all rows,
tab offsets, touch dispatch, changing item counts, swipe/button follow,
subtitles, wrapped labels, taller fonts and a constrained viewport.

Remove the instrumentation with
`git apply -R --unidiff-zero test/simulator/navigation_fixture.patch`
and rebuild before reusing the simulator normally. Never apply this fixture
to a checkout used for device firmware builds.

## Back navigation regression

With `navigation_fixture.patch` applied only in the isolated simulator copy,
run `python3 test/simulator/back_navigation.py /absolute/path/to/program`.
Use `--only '<glob>'` for subsets and `--output-parent` for retained artifacts.
The 29 cases check the live activity stack and preserved Apps selection:
short/held/remapped Back, nested date editors, all offline Apps destinations,
Settings popups, stopping screen cleaning, cancelling network-mode and OPDS
pickers, and explicit second presses. Each case uses a disposable SD.

Before the fix, `heatmap-120` closed ReadingHeatmap on press, then Apps on
release; `app-settings` discarded Apps on launch and returned directly Home.
The shared transition guard now keeps that Back release out of the next
activity. Apps launches retain their caller, including its selection and theme.

Wi-Fi-enabled catalog/File Transfer exits can still intentionally reboot to
Home to reclaim the C3 heap. These tests do not validate live network sessions
or remove their restart/recovery policy. Opening a book still replaces the
navigation stack to release browser memory. Remove the fixture and rebuild
before normal simulator checks; it must never enter a device firmware build.

## Reader settings regression

`python3 test/simulator/reader_settings.py /absolute/path/to/program` uses a
normal X4 simulator build, generated EPUBs and disposable SDs. No fixture is
needed. The 26 cases cover saving the same mark twice through both menus,
all six refresh values, all three bionic modes through three editors,
cancelling a popup, both font selectors, corrupt SD-font fallback, distinct
preview pixels and stopping automatic page turns. State assertions read the
saved settings and bookmark count; pixel comparisons check previews and
the restored popup state, not just whether a screenshot exists.

Use `--only '<glob>'` repeatedly for subsets, `--theme 1` or `--theme 3`,
`--language EN` or `--language ES`, and `--output-parent` for retained
artifacts. Run without `--only` to include all cross-case pixel comparisons.
The synthetic key for Back is `BACK` (or `ESCAPE` in the HAL), not `ESC`.
The harness rejects unsupported key names instead of silently skipping them.

These are host-side composition and workflow tests. They do not prove SD
timing, memory safety on the device, network interoperability or e-ink quality.

After switching test fixtures or updating the SDK, include a clean normal
build in the final visual check. In the October 2026 audit an incremental
simulator binary lost header/preview pixels; rebuilding the same sources
with `pio run -e <env> -t clean` and a fresh `PLATFORMIO_BUILD_CACHE_DIR`
restored them. The harness now rejects a blank header or sample separately,
so equality between two empty preview crops cannot count as success.

## When the simulator stops compiling

- A missing `Hal*` method or a fork-only ESP-IDF include belongs in the fork's
  simulator branch (see the simulator's `FORKING.md`); keep stubs one-line and
  additive so merging upstream simulator changes stays trivial.
- A host-portability problem in shared code (for example libc++ needing a
  complete type where GCC did not) is fixed in this repo.
- Do not edit `.pio/libdeps/*/simulator`; PlatformIO discards it.
