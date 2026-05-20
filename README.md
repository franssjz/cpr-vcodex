# CPR-vCodex Stats

CPR-vCodex Stats is an unofficial firmware fork for the Xteink X4 e-reader.

It builds on CrossPoint Reader, CPR-vCodex, and CrossInk ideas, with a focus on a stable reading experience, clearer reading statistics, faster navigation, cleaner library views, and practical controls for daily reading.

This project is unofficial and is not affiliated with Xteink, CrossPoint Reader, CPR-vCodex, or CrossInk.

Use custom firmware at your own risk. Back up your SD card and reading data before flashing.

---

## Project Overview

The 2.0.0 release centers on:

- clearer Reading Stats and book-specific reading history
- a more useful Home and Recent Books flow
- unified cached covers and consistent placeholders
- compact hold-preview interactions for hidden long-press actions
- CrossInk-style remappable controls
- LyraVcodex2 as the stable CPR-vCodex visual direction
- careful preservation of EPUB, TXT, and XTC reading stability


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
| Theme | LyraVcodex2 |

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


### Release Files

| File | Purpose |
|---|---|
| `.bin` | Flashable firmware image |
| `.json` | Firmware metadata used by release tooling |

---

## Credits

This project builds on work from:

- CrossPoint Reader
- CPR-vCodex
- CrossInk
- the Xteink X4 community

CPR-vCodex Stats is an experimental fork created to explore a more modern, readable, and statistics-aware firmware experience for the Xteink X4.
