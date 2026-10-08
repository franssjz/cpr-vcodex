# Upstream Sync

Read this before syncing with `crosspoint-reader/crosspoint-reader`, comparing
third-party forks, or resolving fork divergence.

## Repository Roles

- `crosspoint-reader-master`: official upstream reference.
- `cpr-vcodex`: this fork, focused on reading consistency and statistics.
- `crosspet` and `papyrix`: third-party forks worth scanning for ideas.
- `freeink-sdk`: hardware SDK submodule/dependency area used by the firmware.

## Sync Strategy

- Prefer selective cherry-picks over blind large merges when upstream changed
  architecture near CPR-vCodex features.
- Keep CPR-vCodex-specific UX and stats behavior unless upstream fixes a real
  bug or improves compatibility.
- Document sync points in `CHANGELOG.md` when the relationship to upstream would
  otherwise be confusing.
- Avoid rewriting history on `master`.

## What To Compare

- Firmware compatibility fixes.
- EPUB/TXT parser bug fixes.
- Memory and flash savings.
- Display/input reliability.
- SDK updates.
- Release workflow changes.

## Conflict Hotspots

- `src/activities/reader/`
- `src/activities/apps/`
- `src/ReadingStatsStore.*`
- `src/util/ReadingStatsAnalytics.*`
- `platformio.ini`
- `scripts/`
- `docs/flash.html` and `docs/assets/site.js`

## 2026-09 Full Merge Of Upstream `develop`

On 2026-09-05 the fork merged upstream `crosspoint-reader/crosspoint-reader`
branch `develop` (head `233f93ff`, previous common commit `63d5094f` from
2026-05-11) instead of cherry-picking. This was a deliberate exception to the
selective-cherry-pick rule above: the Xteink X4 Pro (ESP32-S3, touch, Home key,
frontlight) requires upstream's FreeInkUI touch architecture, and porting it
piecemeal would have re-implemented most of upstream's UI layer.

Decisions taken during that merge (keep them when syncing again):

- Settings, app state, recent books, and Wi-Fi credentials stay on the fork's
  JSON persistence (`JsonSettingsIO`, `SettingsList`). Upstream's
  `PersistableStore` migration was not adopted; upstream's new settings fields
  were added to the fork structs with JSON keys matching upstream's names.
  `screenInverted`, `focusReadingEnabled`, and `moveFinishedToReadFolder` are
  aliases of the fork's `darkMode`, `bionicReading`, and `moveCompletedBooks`.
- The reader font size moved to upstream's point-size model (`fontPointSize`,
  `ReaderFontSizes`); legacy `fontSize` slots are migrated on load.
- Bookmarks and highlights stay on the fork's `BookmarkStore` v5 and
  `BookmarksActivity`; upstream's `EpubReaderBookmarksActivity`/`BookmarkFile`
  are not wired. The fork's StarDict dictionary (`DictionaryStore`) stays;
  upstream's `src/util/Dictionary*` registry is not used.
- Night mode: only the fork's renderer dark mode is applied; upstream's
  per-render panel inversion was removed from `ActivityManager`.
- Build: upstream's `[base]`/`[firmware_tuned]` layout and the `x4pro*`
  environments were adopted (the latter link against the fork's
  `partitions_x4pro.csv`, the stock X4 Pro table, instead of upstream's shared
  `partitions.csv`); the fork keeps
  its generated version (`scripts/git_branch.py`), release scripts, and
  auto-flash flow. `FREEINK_DEVICE_X4/X3` live only in the C3 envs.
- `freeink-sdk` submodule moved to `cb9167d5`.
- Flash budget: upstream's wolfSSL/SecureNet TLS 1.3 stack is built only for
  the `x4pro*` envs (on the C3 the downloader falls back to `esp_http_client`
  on the core mbedTLS CA bundle, so self-signed HTTPS servers are no longer
  accepted there), unused NotoSerif and
  OpenDyslexic font data was removed, and the ten UI languages upstream added
  after the fork's 23 were not adopted (Hebrew stays because the keyboard
  layout table references it). Re-adding any of them needs the C3 release
  image to stay under 0x640000 bytes.
- `HalClock` keeps the fork's UTC API but runs on the SDK `Rtc` driver, so the
  X3 (DS3231) and X4 Pro (BM8563) share one clock path.

Post-merge conflict hotspots are the same as before plus `src/main.cpp`,
`src/CrossPointSettings.*`, `src/SettingsList.*`, the 19 FreeInkUI list screens,
and `src/activities/reader/EpubReaderActivity.cpp`.

## Checks After Sync

### 2026-10-07 Integration

Integration branch: `integration/crosspoint-2026-10-07`, from fork `a4ae01a7`
and upstream `develop` `28971493` (139 commits since `233f93ff`). The SDK is
`9729236ce7b730b81b8fcceec4aa77051b6dfae3`. No history rewrite, commit, push,
release or device flashing is part of this work.

Preservation decisions:

- Keep the September JSON stores and aliases, named KOReader profiles,
  StarDict/history, BookmarkStore v5, stable `bookdata` progress/highlights,
  statistics/Sync Day/achievements, favorites, flashcards and custom shortcuts.
- Keep the fork's TXT/Markdown reader: upstream's virtual-EPUB conversion
  would lose its styled Markdown rendering. The new EPUB word/character
  spacing controls apply to EPUB; TXT retains its existing layout path.
- Keep Lyra Carousel and its custom thumbnail sizing, Bionic Reading, text
  darkness, night-mode composition and framebuffer-loan memory handling.
- Add Library and Plugins to configurable shortcuts. The PSRAM-only cover
  grid retains access to custom shortcuts through Apps.
- Migrate old touch gesture values, paragraph indentation and timezone
  presets. Persist the new independent gestures, Home actions, haptics,
  spacing, library metadata and header clock settings in JSON.
- Keep fork OTA/SD validation, CA-verified C3 manifests, SHA-256-verified downloads, partition
  tables and withdrawn X4 Pro distribution. Plugin networking and content
  crypto now require wolfSSL on C3 too; this does not replace the OTA TLS path.
  The user explicitly authorized the upstream plugin network/SD web APIs.
- Compile the same 24 built-in UI languages and existing built-in font set.
  Keep all upstream translation YAMLs available to custom builds. German
  Liang patterns remain opt-in as before. LTO saves approximately 396 KB of
  linked flash relative to the unoptimized integration.
- Reader plugin events use a separate session tracker, never the persistent
  statistics store. Device rename preserves legacy caches and metadata
  sidecars and updates recent/favorite/statistics paths and cover references.
  Refuse a book rename if no content identity can be obtained safely.

Validation: 560 native tests pass, including new plugin lifecycle,
inserted-hyphen and header date/clock/battery layout tests. All 181 pre-sync
JSON output keys remain (203 now); the core statistics, favorites, flashcards,
achievements and dictionary stores have no semantic changes.
Default C3, production C3 and X4 Pro compile successfully. Final resource
measurements are recorded in `artifacts/upstream-sync/firmware-final-header.log`.
The release builds use `VCODEX_RELEASE_DRY_RUN=1`, so no release counter is
advanced. Ten simulator grayscale boots pass and the fork's Lyra Custom and
Carousel home screens were inspected. See `simulator.md` for the reproducible
HAL snapshot and `test/simulator/cpr-hal-097f44e.patch`.

Final resource measurements (bytes):

| Profile | Static RAM | Linked flash | Final BIN | OTA slot |
| --- | ---: | ---: | ---: | ---: |
| default (X3/X4) | 61,544 | 6,335,429 | 6,349,648 | 6,553,600 |
| gh_release (X3/X4) | 61,544 | 6,258,095 | 6,272,320 | 6,553,600 |
| x4pro (compile-only) | 105,976 | 6,457,490 | 6,462,672 | 8,257,536 |

The C3 release budget report passes at 95.49% linked flash (97.5% limit).
Use the extracted `firmware-release-final.log` for the budget script: the
combined log ends with X4 Pro, so its last size line is not the C3 measurement.
The final BIN also fits the same budget. Syntax checks, staged diff checks,
SDK cleanliness and a no-conflicts Git index pass. `master` remains at
`a4ae01a7`; the integration is staged with `MERGE_HEAD=28971493`, uncommitted.

This is not physical-device validation: test OTA/SD updates,
Wi-Fi/plugin peak heap, sleep/wake and e-ink behavior on recoverable hardware
before publishing, following the stability audit's X3/X4 matrix.

Run at minimum:

```bash
python -m py_compile scripts/git_branch.py scripts/sync_autoflash_firmware.py scripts/pre_release_check.py scripts/firmware_budget_report.py
pio run -e default
```

Use `pio run -e gh_release` before any release-facing change.

### 2026-10-08 release handoff

The user subsequently authorized publishing `1.6.5.1-cpr-vcodex`, including
the integration and the popup, list-selection, reader-setting and Back
navigation fixes recorded in `experience-debugging-2026-10-08.md` and
`stability-audit-2026-09.md`. The uncommitted state and build sizes above are
historical integration checkpoints, not the final release measurements.

The final release uses the `gh_release` C3 profile and the 1.6.5 version base.
The release workflow runs native regressions and budget checks before
publishing; Auto Flash is then synchronized from the published GitHub asset.
The public release budget assets are authoritative for its final BIN size.
Physical X3 and end-to-end OTA validation of this release remain pending;
the earlier dev35 USB installation on one original X4 is not equivalent.
X4 Pro distribution stays withdrawn, and the C3 BIN is not for X4 Classic/X4C.
