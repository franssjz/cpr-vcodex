## 1.2.0.11 — 2026-04-07

**Speed up GitHub Actions: PlatformIO caching, faster clang-format install** (#7)

CI workflows re-download the full ESP32-C3 toolchain (~100MB+) and LLVM apt packages on every run. This adds unnecessary wall time to every push and PR.

### Changes

- **PlatformIO package caching** — Cache `~/.platformio/packages` and `~/.platformio/platforms` via `actions/cache@v4`, keyed on `platformio.ini` hash. Applied to all four workflows (`ci`, `release`, `release_candidate`).

- **Faster clang-format install** — Replace the slow LLVM apt repo setup (`llvm.sh` + `apt-get install clang-format-21`) with `uv pip install --system 'clang-format>=21,<22'`. The `bin/clang-format-fix` script already falls back from `clang-format-21` → `clang-format` and validates version ≥ 21.

- **Drop unnecessary submodule checkout** from the clang-format job — `git ls-files` only lists parent repo files, not submodule contents.

- **Enable uv pip caching** — Flip `enable-cache: false` → `true` on `astral-sh/setup-uv` across all workflows.

- **Bump `upload-artifact` v4 → v6** in `release_candidate.yml` for consistency with `ci.yml`.

### Expected impact

| Optimization | Savings (approx) |
|---|---|
| PlatformIO cache hit | ~60s per job |
| pip clang-format vs apt LLVM | ~20-30s |
| Skip submodule clone | ~10-15s |
| uv cache hit | ~5-10s per job |

The three CI jobs (`clang-format`, `cppcheck`, `build`) already run in parallel — no structural changes needed there.

---
## 1.2.0.10 — 2026-04-07

**Rename release binary from firmware.bin to vcodex-{version}.bin** (#6)

Release artifacts were uploaded as the generic PlatformIO default `firmware.bin`. Users downloading from GitHub Releases had no way to identify the firmware variant or version from the filename alone.

### Changes

- **`scripts/post_build_release.py`**: Changed versioned copy filename from `firmware.{version}-vcodex.bin` to `vcodex-{version}.bin`
- **`.github/workflows/release.yml`**: Updated release asset path to reference the new versioned binary

Release assets will now be named e.g. `vcodex-1.2.0.9.bin` instead of `firmware.bin`.

---
## 1.2.0.9 — 2026-04-07

**Sync with upstream franssjz/cpr-vcodex** (#5)

Merges latest changes from [franssjz/cpr-vcodex](https://github.com/franssjz/cpr-vcodex) `master` into our fork. One conflict in `platformio.ini` resolved by keeping our version (`1.2.0.8`).

### Upstream changes pulled in
- **EpubReaderActivity**: Improved AA text refresh handling — skips harsh half-refresh on grayscale LUT pages, uses `FAST_REFRESH` instead to avoid inverted/flash artifacts in dark mode
- **ActivityResult**: Replaced `std::enable_if_t` with C++20 `requires` constraint
- **JpegToFramebufferConverter / PngToFramebufferConverter**: Simplified member initialization
- **ReaderUtils**: Refresh utility enhancements
- **Translations**: Russian and Ukrainian updates
- **README / CHANGELOG**: Documentation refresh

### Conflict resolution
- `platformio.ini` `[vcodex]` version: kept ours (`1.2.0.8`) over upstream (`1.2.0.7`)

---
## 1.2.0.8 — 2026-04-07

**fix: remove -flto from vcodex_release to fix release build** (#4)

Release workflow fails while CI passes because they use different PlatformIO environments. `vcodex_release` includes `-flto`, but the ESP32-C3 RISC-V toolchain's linker lacks the GCC LTO plugin — producing 291 "plugin needed to handle lto object" errors at link time.

- **Removed `-flto`** from `[env:vcodex_release]` in `platformio.ini`
- Remaining size optimization flags (`-Os`, `-fmerge-all-constants`, `-fno-unwind-tables`, `-fno-asynchronous-unwind-tables`) plus base section's `-ffunction-sections`/`-fdata-sections`/`-Wl,--gc-sections` still provide dead code elimination

The key distinction: CI runs `pio run` (default env, no `-flto`) → passes. Release runs `pio run -e vcodex_release` (has `-flto`) → fails.

---
## 1.2.0.7 — 2026-04-07

**fix: resolve CI build and auto-release workflow failures** (#3)

CI failing on all three check jobs (clang-format, cppcheck, build) and auto-release workflow crashing on every PR merge due to shell injection from PR body and broken version parsing.

### CI fixes (`HomeActivity.cpp`)

- `%d` → `%u` for `uint32_t` return from `getCurrentStreakDays()` (cppcheck `invalidPrintfArgType_sint`)
- Re-wrapped `snprintf` args to satisfy clang-format-21

### Release workflow (`release.yml`)

- **Shell injection**: `${{ github.event.pull_request.body }}` was directly interpolated into shell — PR bodies with C++ code, backticks, etc. were executed as commands. Moved to `env:` block.
- **Version parsing**: `grep 'vcodex.version\s*='` matched nothing because the INI key is `version` under `[vcodex]`, not `vcodex.version`. Replaced with section-aware awk:
  ```bash
  awk '/^\[vcodex\]/{found=1} found && /^version\s*=/{print $NF; exit}' platformio.ini
  ```
- **Version update**: Same root cause — sed now uses range pattern `/^\[vcodex\]/,/^\[/` to scope to the correct section
- **Missing `submodules: recursive`** on checkout → `PackageException` for `open-x4-sdk` symlinks
- **Deprecated actions**: `checkout@v4` → `@v6`, `setup-python@v5` → `@v6`
- **Wrong PlatformIO**: `pip install platformio` → pioarduino-core v6.1.19 via uv (matching CI)

### Cleanup

- Removed broken CHANGELOG.md entry with empty version left by the previous failed release run

---
# Changelog

Brief firmware history for `cpr-vcodex`.

## 1.2.0.7

- bumped the fork release line to `1.2.0.7`
- kept the current firmware feature set and upstream carry-forward state intact while publishing the new release build
- refreshed README metadata to match the new visible fork version and version code

Version code: `2026040707`

## 1.2.0.6

- bumped the fork release line to `1.2.0.6`
- refreshed README metadata and release references to match the current fork state
- kept the firmware feature set unchanged while publishing the updated release line

Version code: `2026040606`

## 1.2.0.5

- rebuilt the current firmware line as `1.2.0.5`
- restored `Lexend` as a reader font family after the later font-pruning pass removed it
- restored the translation files to a clean pre-corruption state after the mojibake regression introduced during the OTA/font refactor work

Version code: `2026040605`

## 1.2.0.4

- reviewed the translation set after the book-id migration work and repaired damaged strings in the language files
- normalized corrupted Hungarian and Lithuanian entries to clean text instead of leaving broken characters on device
- fixed the damaged Italian `ReadMe > Stats` body text
- added a global `Dark Mode` toggle in `Settings > Display`, implemented centrally in the renderer so UI text goes white-on-black while book images keep their original polarity
- added `Text Darkness` for anti-aliased reader text, adapted from the [`crosspet`](https://github.com/trilwu/crosspet) fork
- switched fork OTA metadata from the GitHub Releases API to a simple repo manifest (`ota.json`) to avoid device-side failures reaching `api.github.com`

Version code: `2026040504`

## 1.2.0.1

- rebased the fork onto the upstream `CrossPoint Reader 1.2.0` line
- separated base-version metadata from fork-version metadata in the firmware configuration
- switched the visible `vcodex_release` firmware line to `1.2.0.1` while keeping `CrossPoint Reader 1.2.0` as the tracked upstream base

Version code: `2026040401`

## 1.1.17-vcodex

- added long-press removal with confirmation for recent books in both `Home` and `Apps > Recent Books`
- kept recent-book removal limited to history only, without deleting the actual book file
- persisted recent-book removals cleanly through `/.crosspoint/recent.json`

Version code: `2026040201`

## 1.1.16-vcodex

- reduced sleep-screen flashing by switching the normal sleep rendering paths back to `HALF_REFRESH`
- kept `SleepScreenCache` in place so cached sleep images still load quickly
- refreshed the README header with the contributed text logo and tighter caption spacing

Version code: `2026040102`

## 1.1.15-vcodex

- improved `Reading Heatmap` month navigation so day selection can cross month boundaries naturally
- added direct month switching with the side `Up/Down` buttons in `Reading Heatmap`
- added goal-completion check marks inside heatmap day cells and refined their placement/size

Version code: `2026040101`

## 1.1.14-vcodex

- added `X Small` as a new reader font size with safe settings migration for existing installs
- regenerated the bundled boot/sleep logo header from the refreshed `Logo120.png`
- restored real bold and italic rendering for `Bookerly` when using `X Small`

Version code: `2026033102`

## 1.1.13-vcodex

- renamed the documented firmware line from `crosspoint-vcodex` to `cpr-vcodex`
- refreshed the main README intro to make the `CrossPoint Reader 1.1.1` base and the Codex-assisted evolution of the fork explicit
- bumped firmware release metadata to `1.1.13-vcodex`

Version code: `2026033101`

## 1.1.12-vcodex

- removed `Reading Timeline` as a separate app and folded the workflow around `Reading Stats`, `Reading Heatmap` and `Reading Day`
- split shortcut management into `Location Home and Apps` and a real `Visibility Home and Apps` with `Show / Hidden`
- fixed double-open behavior more robustly by swallowing the inherited `Confirm` release when a new activity becomes active

Version code: `2026033004`

## 1.1.10-vcodex

- improved stability around `Settings > Apps` actions by removing redundant settings saves from export/sync flows and reducing write pressure while reordering shortcuts
- reduced transient memory usage by writing more JSON stores directly to file instead of building intermediate strings in RAM
- optimized `Settings` and shortcut ordering flows with less repeated list rebuilding and lower allocation churn

Version code: `2026033002`

## 1.1.9-vcodex

- promoted the current firmware line to `1.1.9-vcodex`
- refreshed bundled documentation and release metadata to match the current firmware state

Version code: `2026033001`

## 1.1.8-vcodex

- rebuilt the achievements catalog with the new title set and many more finished-book milestones up to `100 books`
- moved achievements names and conditions to real i18n-backed strings instead of English/Spanish-only code paths
- regenerated the translation files from a clean base to fix corrupted language text such as the mojibake seen in `Languages`

Version code: `2026032908`

## 1.1.7-vcodex

- fixed `Achievements` title/description mismatches caused by definition lookup using enum order instead of the real achievement id
- restored coherence between progress targets and labels such as `Belle of the Books`, `Trilogy`, `Finish Line`, `Word Warden`, `Bookmark Hoarder` and `Flag Garden`

Version code: `2026032907`

## 1.1.6-vcodex

- fixed `Achievements` list navigation so the on-screen `Up/Down` controls actually scroll the achievements list
- expanded finished-book achievements with many more milestones, from early reading streaks to a `100 books` trophy line
- improved achievements ordering so milestone branches stay grouped more naturally in the list

Version code: `2026032906`

## 1.1.5-vcodex

- fixed `Achievements` opening behavior so it no longer auto-switches tabs on entry
- aligned `Achievements` navigation with `Settings`: `Confirm` switches tabs and `Up/Down` moves through the list
- expanded the achievements catalogue with more late-game milestones for books, sessions, goal days, streaks, bookmarks and long sessions

Version code: `2026032905`

## 1.1.4-vcodex

- redesigned `Achievements` with top tabs for `Pending` and `Completed`
- expanded the achievements catalogue with more session, books, goal-day, goal-streak and bookmark milestones
- added a stronger ladder of long-session trophies up to a real `Marathon`

Version code: `2026032904`

## 1.1.3-vcodex

- made `Daily Goal` configurable in `Settings > Apps` with `15 min / 30 min / 45 min / 60 min`
- aligned `Goal Streak`, Home stats shortcut, Reading Stats cards and Heatmap logic with the selected goal
- updated achievement goal-day and goal-streak progress so they stay coherent after changing the configured goal

Version code: `2026032903`

## 1.1.2-vcodex

- formalized firmware versioning and version code tracking
- added a concise changelog to make release-to-release changes easier to follow
- kept the current feature set stable as the new documented release line

Version code: `2026032902`

## 1.1.1-vcodex

- added `Achievements` with retroactive unlock bootstrap, popups and reset controls
- added `Sync with prev. stats` for achievements
- added shortcut visibility management and separate Home/Apps ordering
- added `/ignore_stats/` exclusion for stats, sessions, timeline, heatmap and achievement-related tracking
- polished Home/Apps navigation and fixed several app-entry double-open issues

Version code: `2026032901`

## 1.1.0-vcodex

- added `Reading Heatmap`, `Reading Day` and `Reading Timeline`
- expanded `Reading Stats` with richer per-book and aggregate views
- added `Sync Day` diagnostics, configurable date format and time zone selection
- improved Home with dynamic reading stats shortcut and configurable shortcut placement
- added a global `Bookmarks` app for EPUB bookmarks

## 1.0.0-vcodex

- established the fork identity and `Lyra Custom` default theme
- added the first `Sync Day + fallback` model for day-coherent reading analytics
- added the first wave of reading-focused UX improvements, custom sleep handling and stats workflows
- kept full CrossPoint Reader compatibility as the base firmware
