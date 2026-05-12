# CPR-vCodex Stats

CPR-vCodex Stats is an unofficial, Codex-assisted firmware fork for the
Xteink X4 e-reader. It is based on CPR-vCodex, which is itself based on
CrossPoint Reader, but this repository is now its own experiment: a reading
firmware build focused on deeper reading statistics, completion estimates, and
more useful reader analytics.

This project is not affiliated with Xteink, CrossPoint Reader, or the upstream
CPR-vCodex maintainer. It is a personal fork built with AI-assisted development
using Codex and tested primarily for Daniel's own Xteink X4 workflow.

Use it at your own risk. Custom firmware can fail to flash, behave differently
than expected, lose reading data, or require recovery flashing. Back up your SD
card and exported reading stats before installing any release from this fork.

## Current Release

| Item | Value |
|---|---|
| Fork name | `CPR-vCodex Stats` |
| Firmware focus | Reading statistics, completion estimates, and analytics |
| Target device | Xteink X4 |
| First fork release name | `Stats Preview 0.1.0` |
| Published firmware tag | [`1.2.0.48-cpr-vcodex`](https://github.com/danielc0603/cpr-vcodex-stats/releases/tag/1.2.0.48-cpr-vcodex) |
| Flashable file | `1.2.0.48-cpr-vcodex.bin` |
| Changelog | [CHANGELOG.md](./CHANGELOG.md) |

The published tag still uses the inherited CPR-vCodex-compatible release format
because the current build and release scripts expect that scheme. Going forward,
this fork will describe releases with a simpler fork-facing name such as
`Stats Preview 0.1.0`, while the firmware artifact may keep the compatible tag
format until the release tooling is intentionally renamed.

## What This Fork Changes

The first goal is to make reading progress more understandable without adding
friction while reading.

The current stats work adds:

- estimated time left for the whole book;
- estimated time left for the current chapter;
- average reading session length;
- average reading-day length;
- persisted chapter reading-time state for better chapter estimates;
- browser stats-editor support for the new chapter estimate fields;
- release artifacts and auto-flash metadata pointing to this fork's published
  firmware.

These estimates are intentionally conservative. If there is not enough reading
history yet, the device should say to read more before trusting the estimate.
The chapter estimate becomes useful only after the firmware has watched progress
inside the current chapter for a while.

## What Is Inherited

This fork still includes many existing CPR-vCodex and CrossPoint Reader features,
including:

- EPUB, TXT, Markdown, and XTC reading support;
- existing reading stats, heatmap, day detail, and reading profile screens;
- manual reading-time correction;
- achievements;
- bookmarks and recent books;
- OPDS and KOReader Sync related features;
- flashcards;
- sleep-screen tools;
- browser-based file transfer and settings tools;
- multilingual UI support.

Those inherited features are not claimed as newly created here. This repository
builds on them and changes the direction toward more detailed reading analytics.

## Installation

The easiest manual install path is:

1. Open the latest release:
   [1.2.0.48-cpr-vcodex](https://github.com/danielc0603/cpr-vcodex-stats/releases/tag/1.2.0.48-cpr-vcodex).
2. Download `1.2.0.48-cpr-vcodex.bin`.
3. Turn on and unlock the Xteink X4.
4. Open [xteink.dve.al](https://xteink.dve.al/) in Chrome or Edge.
5. Choose the downloaded `.bin` file in the OTA flash controls.
6. Flash the firmware and wait for completion.

The browser auto-flash files in `docs/firmware/` have also been synchronized to
the current fork release, but the safest first install path is still to download
the `.bin` release asset directly and know exactly which file is being flashed.

## Reading Stats Behavior

The stats system tracks reading sessions and progress while books are open. The
new estimate fields are derived from that existing stats flow.

Book time-left estimate:

- uses tracked progress gained during valid reading sessions;
- waits until there is enough tracked reading time or progress gain to make a
  reasonable estimate;
- displays an approximate remaining duration, or `More data` when confidence is
  too low.

Chapter time-left estimate:

- tracks time spent in the current chapter;
- records the chapter progress point where timing started;
- estimates remaining chapter time from progress made inside that chapter;
- resets when the current chapter changes.

Average session:

- divides total recorded reading time by counted reading sessions.

Average reading day:

- divides total recorded reading time by days that have nonzero reading time.

## Data And Privacy

Reading stats are stored locally on the device and can be exported for editing.
The browser stats editor is designed to work locally in the browser. It should
not upload private reading data.

Before flashing, exporting, importing, or manually editing stats, keep a backup
of:

- the SD card;
- exported reading stats;
- any books or documents that are not stored elsewhere.

## Release Files

A GitHub release can contain several files. For this fork:

- `.bin` is the firmware file to flash;
- `.json` is build metadata;
- `firmware-budget.json` is a machine-readable firmware size and RAM report;
- `firmware-budget.md` is the same budget report in a readable format.

For normal flashing, use the `.bin` file.


## Credits

This fork exists because of prior work from:

- CrossPoint Reader, the original firmware base;
- CPR-vCodex, the upstream fork this repository was cloned from;
- the Xteink X4 community;

Again: this project is unofficial, unaffiliated, experimental, and provided with
no warranty.
