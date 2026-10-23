# Issue #207: stable page numbers and the statistics power shortcut

Implementation reference: [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk)
at `9914146eeae7b46b300f475a16c32426fc02ec1f`, particularly `Epub::loadXLocations`,
`Epub::resolveReferencePage`, and `docs/reader-features.md`. This is a selective
adaptation of the metadata contract and display behavior, not a fork merge.

## Use

- Settings > Reader > Customise Status Bar > **Stable Page Numbers (EPUB)**.
  It defaults off. When enabled, the page counter uses whole-book reference
  pages from `META-INF/x-locations.json`, prepared with the CrossInk optimizer
  or Inky. Both v1 `x-locations` and legacy `crossink-locations` are accepted.
  Character-based metadata takes precedence over legacy word-based metadata.
- Reference totals do not change with font size, spacing, orientation or
  incremental pagination. As in CrossInk, the current reference page is an
  estimate obtained from the chapter's rendered-page fraction. It can move
  as that fraction changes during repagination/indexing; these are not exact
  publisher pages or a count of physical page turns.
- Missing, invalid, oversized or memory-constrained metadata falls back to
  chapter pages. The existing chapter/book progress bars retain their meaning.
  TXT/Markdown and XTC retain their normal page counts. This change does not
  generate metadata or alter the fork's existing EPUB optimizer.
- Settings > Controls > **Long Power Button Press** > **Reading Stats**.
  Sleep remains the default. Hold for over 400 ms to open the statistics
  overview, including the daily goal. Back returns to the previous screen.
  A hold fires once and its release cannot also turn a page, refresh or sleep.
  With short-click Sleep selected, a short click sleeps on release; a hold
  opens statistics. Screenshot combinations and wake-release guards retain
  priority. X4 Pro double-click behavior is preserved in code but untested on
  hardware; this does not restore X4 Pro distribution.

## Resource and compatibility boundaries

- No EPUB cache format or reading-history schema changes. Reference-page
  display does not reinterpret stored reading time, sessions or progress.
- The manifest is limited to 64 KiB and 1,024 spine entries, extracted via the
  existing streaming ZIP path to a temporary SD file. Filtered JSON has a hard
  48 KiB allocation cap, including allocator headers; only 8 bytes per spine
  remain after loading. Heap admission reserves an additional 32 KiB for ranges,
  filter/ZIP overhead and reader headroom. Failure is optional, never book-fatal.
- Ranges must cover the declared units contiguously, without duplicate indices,
  gaps, overlap, negative counts or overflow. Declared totals must match the
  unit count and units per page. Loading and SD access occur outside rendering;
  resolving the footer has no allocation or SD access.
- Opening statistics directly from a reader pauses its active session after
  crediting the elapsed reading time. Returning resets the interaction clock;
  sleeping/exiting from statistics ends the paused session without crediting
  time spent on the statistics screen. Existing history remains unchanged.

## Verification

- `test/stable_page_numbers`: nine native tests for character and word
  metadata, boundaries, empty spines, clamping, invalid/incomplete ranges,
  overflow, stale-state clearing, filtered streaming and bounded allocation.
- `test/simulator/stable_pages_power_stats.py`: fourteen disposable-SD X4 boots
  cover two font sizes, missing/invalid metadata, disabled setting, low-heap
  fallback, repeated holds and Back, short page-turn/sleep, long statistics
  with short Sleep/Refresh, sleep directly from statistics, and default
  hold-to-sleep. Footer/body pixel
  comparisons verify fixed totals, fallback and return to the same page.
  Saved reading time and daily totals exclude the statistics visit. An additional
  device-settings workflow enables and persists both options.
- Physical X3/X4 button timing, wake, SD pressure and power behavior still need
  device verification. No firmware was flashed or published.

Build results and budget reports are retained under `artifacts/issue-207/`.

Both final X4 builds passed the 97.5% flash budget:

| Environment | ELF flash | Packaged BIN | Static RAM |
|---|---:|---:|---:|
| default | 6,357,747 B (97.01%) | 6,371,968 B | 62,088 B |
| gh_release (dry run) | 6,278,521 B (95.80%) | 6,292,736 B | 62,088 B |

The published 1.6.5.6 BIN/JSON were backed up before the local release build
and restored afterward. The BIN SHA-256 remains
`a2d20fb28bb993952c45df6b669ba7fddf10dd7701b874fd6856968855cd8456`.
No commits, pushes, version bumps, releases or device flashing were performed.
