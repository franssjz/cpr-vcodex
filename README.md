# CPR-vCodex Stats

CPR-vCodex Stats is an unofficial firmware fork for the Xteink X4 e-reader.

It is based on CPR-vCodex, which itself is based on CrossPoint Reader. This
fork keeps the same core reading foundation but focuses more on reading
statistics, reader customization, cleaner menus, and a more polished e-reader
experience.

This project is unofficial and is not affiliated with Xteink, CrossPoint
Reader, or CPR-vCodex. Use it at your own risk. Custom firmware can fail to
flash, behave unexpectedly, lose data, or require recovery flashing.

## Why this fork exists

This fork was made to make the Xteink X4 feel better as a daily e-reader.

Main goals:

- better reading statistics
- clearer reading progress
- useful time-left estimates
- cleaner Home, Library, and Reader menus
- improved reader customization
- custom font support
- a more focused e-reader UI

The goal is not to replace CrossPoint Reader completely. This fork selectively
builds on it while keeping the firmware lightweight enough for the ESP32-C3.

## Main changes

Compared with upstream firmware, this fork focuses on:

- expanded Reading Stats
- Reading Profile summaries
- book and chapter time-left estimates
- cleaner Library / Bookshelf views
- LyraVcodex2 as the main visual direction
- custom SD-card reader fonts
- reader quick settings improvements
- status bar customization
- removed Flashcards to recover firmware space

Other inherited features such as EPUB/TXT/XTC reading, OPDS, KOReader Sync,
bookmarks, favorites, sleep tools, file transfer, and OTA support remain based
on upstream work unless otherwise changed.

## Installation

1. Open the latest release on GitHub.
2. Download the `.bin` firmware file.
3. Turn on and unlock the Xteink X4.
4. Open https://xteink.dve.al/ in Chrome or Edge.
5. Select the downloaded `.bin` file.
6. Flash the firmware and wait for completion.
7. Restart the device if needed.

## Custom fonts

Custom reader fonts use the `.cpfont` format.

Recommended SD card folders:

```text
/.fonts
/fonts
cat > README.md <<'EOF'
# CPR-vCodex Stats

CPR-vCodex Stats is an unofficial firmware fork for the Xteink X4 e-reader.

It is based on CPR-vCodex, which itself is based on CrossPoint Reader. This
fork keeps the same core reading foundation but focuses more on reading
statistics, reader customization, cleaner menus, and a more polished e-reader
experience.

This project is unofficial and is not affiliated with Xteink, CrossPoint
Reader, or CPR-vCodex. Use it at your own risk. Custom firmware can fail to
flash, behave unexpectedly, lose data, or require recovery flashing.

## Why this fork exists

This fork was made to make the Xteink X4 feel better as a daily e-reader.

Main goals:

- better reading statistics
- clearer reading progress
- useful time-left estimates
- cleaner Home, Library, and Reader menus
- improved reader customization
- custom font support
- a more focused e-reader UI

The goal is not to replace CrossPoint Reader completely. This fork selectively
builds on it while keeping the firmware lightweight enough for the ESP32-C3.

## Main changes

Compared with upstream firmware, this fork focuses on:

- expanded Reading Stats
- Reading Profile summaries
- book and chapter time-left estimates
- cleaner Library / Bookshelf views
- LyraVcodex2 as the main visual direction
- custom SD-card reader fonts
- reader quick settings improvements
- status bar customization
- removed Flashcards to recover firmware space

Other inherited features such as EPUB/TXT/XTC reading, OPDS, KOReader Sync,
bookmarks, favorites, sleep tools, file transfer, and OTA support remain based
on upstream work unless otherwise changed.

## Installation

1. Open the latest release on GitHub.
2. Download the `.bin` firmware file.
3. Turn on and unlock the Xteink X4.
4. Open https://xteink.dve.al/ in Chrome or Edge.
5. Select the downloaded `.bin` file.
6. Flash the firmware and wait for completion.
7. Restart the device if needed.

## Custom fonts

Custom reader fonts use the `.cpfont` format.

Recommended SD card folders:

```text
/.fonts
/fonts
