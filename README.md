# CPR-vCodex Stats

CPR-vCodex Stats is an unofficial firmware fork for the Xteink X4 e-reader.

It is based on CPR-vCodex and CrossPoint Reader, but focuses on creating a cleaner, faster, and more reader-focused e-ink experience with improved reading statistics, navigation, library organization, and reader customization.

This project is unofficial and is not affiliated with Xteink, CrossPoint Reader, or CPR-vCodex.

Use custom firmware at your own risk. Always back up your SD card and reading data before flashing.

---

# Main Goals

The goals of this fork are to:

- provide reading estimates and reading information based on user reading behavior
- improve Reading Stats and reading history systems
- improve reader customization
- improve library organization and navigation
- modernize the Xteink X4 reading experience
- keep the firmware lightweight enough for the ESP32-C3

The goal is not to completely replace CrossPoint Reader, but to build on it while improving the overall reading experience.

---

# Main Features

## Reading Stats

Expanded Reading Stats system including:

- current reading tracking
- total reading time
- reading streaks
- started books
- finished books
- time-left estimates
- book-specific stats pages
- reading progress summaries
- reading session history
- hidden/remove-from-stats support

Book-specific stats pages include:

- current chapter
- book progress
- chapter progress
- reading confidence
- reading pace information

---

## Reader Navigation

Reader navigation improvements include:

- hold-Back quick navigation menu
- Recent Books quick switcher
- nested Back behavior
- Book Info placeholder pages
- improved Reader Quick Settings behavior

The navigation system is designed to reduce friction while reading.

---

## Browse Files & Library

Library and Browse Files behavior has been redesigned for speed and consistency.

Features include:

- lightweight placeholder-based rendering
- improved card layouts
- Bookshelf-style library browsing
- Continue Reading
- To Read
- Finished organization
- virtual Finished/Read separation
- configurable Browse Files layouts
- cleaner navigation consistency

The firmware avoids heavy cover loading during normal library browsing to preserve responsiveness.

---

## Reading Profile

Reading Profile was inherited from upstream CPR-vCodex and remains available as part of the firmware.

It provides:

- reading consistency information
- pacing trends
- engagement summaries
- reading habit information
- reading summaries and predictions

---

## Custom Fonts

Custom font support is inherited and adapted from upstream CPR-vCodex/CrossPoint functionality.

Features include:

- Reader Font Family selection
- Manage Fonts system
- install/uninstall support
- downloadable font catalog
- .cpfont support
- built-in font fallback behavior

---

## Themes

Current themes include:

- Lyra
- LyraVcodex
- LyraVcodex2
- RoundedRaff

RoundedRaff is adapted from upstream CrossPoint work.

LyraVcodex2 is the primary UI direction for this fork and focuses on:

- cleaner spacing
- softer card layouts
- better dashboard organization
- more modern e-reader presentation

---

# Installation

1. Download the latest .bin release.
2. Turn on and unlock the Xteink X4.
3. Open:
   https://xteink.dve.al/
4. Select the firmware .bin.
5. Flash the firmware.
6. Restart the device if needed.

---

# Release Files

| File | Purpose |
|---|---|
| .bin | firmware image |
| .json | firmware metadata |

---

# Credits

This project builds on work from:

- CrossPoint Reader
- CPR-vCodex
- the Xteink X4 community

CPR-vCodex Stats is an unofficial experimental fork created to explore a more modern and reader-focused direction for the Xteink X4.