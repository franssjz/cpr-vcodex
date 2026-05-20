# CPR-vCodex Stats

CPR-vCodex Stats is an unofficial firmware fork for the Xteink X4 e-reader.

It builds on CrossPoint Reader, CPR-vCodex, and CrossInk ideas, with a focus on a stable reading experience, clearer reading statistics, faster navigation, cleaner library views, and practical controls for daily reading.

This project is unofficial and is not affiliated with Xteink, CrossPoint Reader, CPR-vCodex, or CrossInk.

Use custom firmware at your own risk. Back up your SD card and reading data before flashing.

---

## Project Overview

CPR-vCodex Stats 2.0.0 is the stabilized release line built from the restored 1.9.19 baseline. It consolidates the major work from the 1.7.5, 1.8.x, and 1.9.x development cycles into a single reader-focused firmware build.

The 2.0.0 release centers on:

- clearer Reading Stats and book-specific reading history
- a more useful Home and Recent Books flow
- unified cached covers and consistent placeholders
- compact hold-preview interactions for hidden long-press actions
- CrossInk-style remappable controls
- LyraVcodex2 as the stable CPR-vCodex visual direction
- careful preservation of EPUB, TXT, and XTC reading stability

The 1.9.20 and 1.9.21 experimental input/orientation changes are not part of this stabilized 2.0.0 baseline.

---

## CPR-vCodex Goals

CPR-vCodex is meant to make the Xteink X4 feel more organized, book-focused, and comfortable for daily reading without turning the firmware into a heavy app platform.

Guiding principles:

- keep the reader fast on ESP32-C3 hardware
- prefer cached metadata and thumbnails over repeated SD-card scans
- keep controls discoverable without cluttering the reading screen
- make Reading Stats useful on-device, not only in exported data
- avoid exposing settings that do not map to real working firmware behavior
- preserve compatibility with the core CrossPoint Reader reading experience

---

## Main Features

| Area | What changed |
|---|---|
| Reading Stats | Book detail pages, reading totals, streaks, estimates, session summaries, covers, and manual time adjustment |
| Home | Current-book hero, Recent Books preview, up-next shortcut, metadata repair, and clean shortcut separation |
| Recent Books | Large cover grid, current-book exclusion where practical, shared cover lookup, placeholders, and hold-Back access from the reader |
| Library | To Read and Finished shelves, compact folder/author rows, standardized book tiles, and faster Browse Files navigation |
| Covers | Shared cached cover lookup across Recent Books, Home, Reading Stats, To Read, and Library where practical |
| Controls | CrossInk-style compact Controls UI with remappable power, front, side, and menu actions |
| Reader | Text darkness modes, safer anti-aliased rendering, margin wiring, orientation target/cycle controls, and stable EPUB/TXT/XTC behavior |
| Theme | LyraVcodex2 is the canonical CPR-vCodex UI path for 2.0.0 |

---

## Controls System Overview

Controls use a CrossInk-style compact layout and mapped action model adapted for CPR-vCodex.

Supported control areas include:

- short and long power button actions
- front button reader mappings
- side button reader mappings
- long-press menu shortcut actions
- orientation-aware reader controls
- fixed orientation target and cycle-orientation behavior
- hold-Back Recent Books access from the reader
- Home long-press shortcuts for current book and up-next behavior

The Controls screen is designed to show concise labels and real choices. Unsupported or unsafe actions are hidden instead of being presented as no-op settings.

Known intentionally hidden or limited actions:

- screenshot appears only where the firmware has a real safe action path
- file transfer is not shown in unsafe reader/menu contexts
- unrelated reader/book actions are not shown in simplified power-button pickers
- orientation actions are only shown where the reader orientation state is available

---

## Reading Stats Overview

Reading Stats tracks reading behavior and presents it in clearer book-focused views.

Current highlights:

- total reading time
- today and streak tracking
- started and finished books
- book-specific stats pages
- chapter and book progress
- time-left estimates
- reading pace and confidence details
- session summaries after reading
- Reading Stats detail access from the reader
- book covers on Reading Stats detail pages when cached covers are available
- manual add/remove reading time on book-specific stats pages

Manual time correction is useful when time was missed, duplicated, or affected by sleep/wake behavior. Open a book's Reading Stats detail and hold Select to adjust reading time for a specific day. The adjustment is stored in the Reading Stats record and cannot reduce a day below zero.

---

## Home and Recent Books Overview

Home is centered around the active reading flow:

- current-book card opens the current book
- Recent Books preview opens the Recent Books screen
- holding the Recent Books preview opens the up-next recent book directly
- holding the current-book card starts remove-from-recents confirmation
- Home repairs missing display titles from Reading Stats, cached metadata, or filename fallback without scanning the whole SD card
- Home and Apps shortcut visibility are separated so each surface can stay focused

Recent Books is the main quick-switcher for active reading:

- current book is excluded where practical
- missing files are skipped safely
- cached covers are reused when available
- placeholders are used when covers are missing
- selected metadata is kept separate from the cover grid
- hold actions are shown through compact temporary previews

Reader hold-Back opens Recent Books directly for fast switching while reading.

---

## Library and Covers

Library and Browse Files prioritize speed and predictable navigation.

Current behavior:

- To Read and Finished shelves stay separate
- Library root keeps compact folder/author rows
- raw SD browsing stays fast and avoids full recursive cover scans
- To Read and regular Library views use cached covers where practical
- Finished Books uses lightweight placeholders to avoid unnecessary cover work
- book/file tiles use consistent book-shaped dimensions where practical
- cover lookup is shared across major reading surfaces where safe

The firmware avoids recursive SD scans and large bitmap caches because the ESP32-C3 has tight RAM and flash limits.

---

## Reader Experience

Reader-focused improvements include:

- LyraVcodex2 as the primary UI style
- improved Recent Books quick switching
- Reading Stats access from inside the reader
- reader text darkness modes, including Extra Dark and Crisp / Nitido
- Reader Screen Margin wired through reader layout
- chapter skip direction fixes
- orientation target and cycle-orientation behavior
- safer anti-aliased rendering behavior
- darker bundled Bookerly font assets from the 1.9 line
- EPUB, TXT, and XTC stability preserved through the 2.0.0 stabilization pass

Reader Screen Margin is applied through reader viewport layout. EPUB and TXT paths use the configured margin; raw library browsing is unaffected.

---

## Installation and Build Basics

### Flashing

1. Download the latest firmware `.bin` release.
2. Turn on and unlock the Xteink X4.
3. Open:
   https://xteink.dve.al/
4. Select the firmware `.bin`.
5. Flash the firmware.
6. Restart the device if needed.

### Local Build

For local development from the repository root:

```bash
pio run -e default
```

Simulator builds use:

```bash
pio run -e simulator
```

Release builds and published auto-flash assets should come from the project release workflow, not from arbitrary local files.

### Release Files

| File | Purpose |
|---|---|
| `.bin` | Flashable firmware image |
| `.json` | Firmware metadata used by release tooling |

---

## Version History

This history summarizes the user-facing milestones from CPR-vCodex 1.7.5 through the 2.0.0 stabilization release. It is not a raw git log.

### 2.0.0 - Stable CPR-vCodex Baseline

2.0.0 is a stabilization release built from the restored 1.9.19 behavior.

Highlights:

- keeps LyraVcodex2 as the canonical CPR-vCodex UI path
- preserves compact hold-preview interactions
- preserves CrossInk-style Controls layout and mapped controls
- preserves Reading Stats manual add/remove time
- preserves Home metadata repair and Recent Books behavior
- preserves unified cover and placeholder behavior
- keeps EPUB, TXT, and XTC reader behavior stable
- does not include the 1.9.20 or 1.9.21 experimental input/orientation consume-state changes

### 1.9.x - Controls, Reader, and Stabilization

The 1.9 series focused on controls, reader polish, Reading Stats detail behavior, and stabilization.

Major changes:

- LyraVcodex2 became the canonical CPR-vCodex theme path
- theme switching was removed from the normal user flow to reduce maintenance complexity
- CrossInk-style controls were adapted into CPR-vCodex
- power, front, side, and menu controls became remappable where real firmware actions exist
- compact Controls UI replaced cramped inline controls
- orientation-aware reader controls were added
- fixed orientation target and Cycle Orientations behavior were added
- long-press menu actions were simplified to safe reader actions
- hold-Back in the reader opens Recent Books
- hold-preview interactions made hidden long-press actions discoverable
- compact hold-preview rectangles replaced always-visible bottom hints
- Reading Stats detail pages gained manual add/remove reading time
- Reading Stats opened from the reader can return to the reader context
- reader text darkness settings were wired into the actual render path
- Extra Dark and Crisp / Nitido reader styles remained available
- anti-aliased rendering was hardened to avoid mixed opacity artifacts
- bundled reader font behavior was improved, including darker Bookerly assets where available
- Reader Screen Margin wiring was checked and preserved
- chapter skip direction was corrected so forward/back long presses move in the expected direction
- Home metadata repair was added for missing titles and authors
- Browse Files and Library hold actions gained temporary hold previews

### 1.8.x - Library, Recent Books, Covers, and Home Flow

The 1.8 series focused on the reading surfaces people use most often: Home, Library, Browse Files, Recent Books, and Reading Stats covers.

Major changes:

- Library root was reorganized into To Read, Finished, and compact author/folder rows
- large author/folder cards were replaced with a compact folder list
- Browse Files Back behavior was cleaned up so folder navigation returns to the new Library root
- Bookshelf Columns and old file-browser view options were removed where they no longer applied
- Finished Books was changed to use lightweight placeholders instead of expensive cover resolution
- To Read, regular Library, Recent Books, Home, and Reading Stats moved toward shared cached cover lookup
- Reading Stats book detail pages gained cover consistency with Recent Books
- Recent Books was upstreamed/adapted from CrossInk-style grid behavior
- Recent Books excludes the current book where practical
- Recent Books uses a selected-book metadata band above the cover grid
- Recent Books cover lookup was cached per visible page/session to avoid focus-move slowdown
- hold-Back Recent Books and main Recent Books moved toward shared behavior
- Home was simplified around the current-book hero and Recent Books access
- Home Recent Books preview opens Recent Books on short select
- holding the Home Recent Books preview opens the up-next book
- Home and Apps shortcut visibility were separated
- duplicate Stats / Reading Stats shortcuts were cleaned up
- Recent Books was hidden from Apps by default when Home exposes it directly
- cover rendering became more consistent across Sleep Screen, Home, Recent Books, Reading Stats, and Library where cached covers exist

### 1.7.5 - Starting Point for the Stabilization Line

1.7.5 is the baseline this README treats as the beginning of the documented 2.0.0 evolution.

The later work built on that baseline by improving:

- Reading Stats clarity and book-specific detail
- Home active-reading navigation
- Library and Browse Files structure
- Recent Books as the primary active-book switcher
- cached cover consistency
- controls discoverability and remapping
- LyraVcodex2 UI stability
- EPUB/TXT/XTC reader behavior preservation

---

## Credits

This project builds on work from:

- CrossPoint Reader
- CPR-vCodex
- CrossInk
- the Xteink X4 community

CPR-vCodex Stats is an experimental fork created to explore a more modern, readable, and statistics-aware firmware experience for the Xteink X4.
