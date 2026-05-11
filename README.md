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
| Published firmware tag | [`1.2.0.47-cpr-vcodex`](https://github.com/danielc0603/cpr-vcodex-stats/releases/tag/1.2.0.47-cpr-vcodex) |
| Flashable file | `1.2.0.47-cpr-vcodex.bin` |
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
   [1.2.0.47-cpr-vcodex](https://github.com/danielc0603/cpr-vcodex-stats/releases/tag/1.2.0.47-cpr-vcodex).
2. Download `1.2.0.47-cpr-vcodex.bin`.
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

## Release Naming

This fork is starting fresh in documentation with fork-facing release names:

- `Stats Preview 0.1.0`: first Daniel/Codex stats-focused release.

The current build scripts still publish firmware tags like:

- `1.2.0.47-cpr-vcodex`

That inherited tag means:

- base firmware line: `1.2.0`;
- release number: `44`;
- compatibility suffix: `cpr-vcodex`.

A future cleanup can rename the build scripts, artifact suffix, release regexes,
and auto-flash sync rules to a dedicated `cpr-vcodex-stats` or `stats` scheme.
That should be done carefully because the release workflow, firmware metadata,
and auto-flash page all depend on the current format.

## Development Workflow

Typical future work on this fork:

1. Decide the reading-stat or firmware behavior to change.
2. Inspect the relevant firmware files.
3. Make the smallest safe code change.
4. Build with PlatformIO.
5. Flash and test on hardware.
6. Update the changelog.
7. Publish a new GitHub release when the build is worth sharing.

Useful commands:

```bash
pio run -e default
pio run -e gh_release
python3 scripts/pre_release_check.py --tag 1.2.0.47-cpr-vcodex
python3 scripts/sync_autoflash_firmware.py --repo danielc0603/cpr-vcodex-stats
```

## Upstream Relationship

`upstream` refers to the original CPR-vCodex repository. `origin` refers to
Daniel's fork.

The upstream author can continue publishing their own updates. This fork does
not automatically receive those changes. Future updates can be reviewed,
merged, skipped, or adapted depending on whether they fit this fork's reading
stats direction.

This fork should preserve stability first. The Xteink X4 uses an ESP32-C3 with
limited RAM and no PSRAM, so new analytics should stay lightweight and avoid
expensive work in reading/render loops.

## Credits

This fork exists because of prior work from:

- CrossPoint Reader, the original firmware base;
- CPR-vCodex, the upstream fork this repository was cloned from;
- the Xteink X4 community;
- Codex, used as Daniel's AI coding assistant for this fork.

Again: this project is unofficial, unaffiliated, experimental, and provided with
no warranty.
