# crosspoint-vcodex

`crosspoint-vcodex` is a reading-focused fork of **CrossPoint Reader** for the **Xteink X4**.

It keeps the strong CrossPoint base and adds a more polished day-to-day reading experience:

- better Home workflow
- configurable Home and Apps shortcuts
- coherent date-based reading stats
- heatmap and timeline views
- achievements
- EPUB bookmarks with a global app
- visible firmware version code on boot

This project is **not affiliated with Xteink**.

## At a glance

| Item | Value |
|---|---|
| Base firmware | CrossPoint Reader |
| Device | Xteink X4 |
| Current release | `1.1.2-vcodex` |
| Version code | `2026032902` |
| Release notes | [CHANGELOG.md](./CHANGELOG.md) |
| Recommended install | browser OTA fast flash |

## Easy installation

For most users, this is the easiest way to install the firmware:

1. Download the latest `crosspoint-vcodex` firmware from [GitHub Releases](https://github.com/franssjz/crosspoint-reader-codex/releases).
2. Turn on and unlock your Xteink X4.
3. Open [xteink.dve.al](https://xteink.dve.al/).
4. In `OTA fast flash controls`, select the downloaded firmware file.
5. Click `Flash firmware from file`.
6. Select the device when the browser asks.
7. Wait for the installation to finish.
8. Restart the device:
   press the bottom-right button once, then press and hold the right power button.

> To return to the original CrossPoint Reader later, repeat the same process with the original firmware file.

## Screenshots

<p align="center">
  <img src="./docs/images/Main.bmp" alt="Main" width="230" />
  <img src="./docs/images/ReadingStats.bmp" alt="Reading Stats" width="230" />
  <img src="./docs/images/ReadingStatsExtended.bmp" alt="Reading Stats Extended" width="230" />
</p>
<p align="center">
  <img src="./docs/images/ReadingHeatmap.bmp" alt="Reading Heatmap" width="230" />
  <img src="./docs/images/ReadingDay.bmp" alt="Reading Day" width="230" />
  <img src="./docs/images/ReadingTimeline.bmp" alt="Reading Timeline" width="230" />
</p>
<p align="center">
  <img src="./docs/images/ReadingStatsBook.bmp" alt="Reading Stats Book Detail" width="230" />
</p>

## What this fork adds

| Feature | What it adds | More info |
|---|---|---|
| `Lyra Custom` default theme | reading-first default presentation from first boot | [Home and Apps](#home-and-apps) |
| `Home + Shortcuts` | configurable Home/Apps placement and reorderable shortcut lists | [Home and Apps](#home-and-apps) |
| `Sync Day` | manual Wi-Fi day sync plus fallback-day logic | [Sync Day and date model](#sync-day-and-date-model) |
| `Reading Stats` | richer totals, started books, per-book detail and extended trends | [Reading analytics suite](#reading-analytics-suite) |
| `Reading Heatmap` | monthly calendar view of reading intensity | [Reading analytics suite](#reading-analytics-suite) |
| `Reading Day` | drill-down into one specific reading day | [Reading analytics suite](#reading-analytics-suite) |
| `Reading Timeline` | recent-history view by day | [Reading analytics suite](#reading-analytics-suite) |
| `Achievements` | console-style milestones and optional popups | [Achievements](#achievements) |
| `Bookmarks` | EPUB bookmarks plus a global bookmarks app | [Bookmarks](#bookmarks) |
| `Sleep tools` | folder selection, preview and sequential/shuffle behavior | [Sleep](#sleep) |
| `Date controls` | global date format and time zone settings | [Settings](#settings) |
| `Version code` | exact build identification shown on boot | [Versioning](#versioning) |

## 5-minute start

If you just flashed the fork and want the main extras immediately:

1. Open `Home > Sync Day`
2. Connect to Wi-Fi and sync the date
3. Open a book and read normally
4. Open `Home > Stats` or `Apps > Reading Stats`
5. Open `Apps > Reading Heatmap` or `Apps > Reading Timeline`

That is enough to start using the core value of the fork: coherent day-based reading analytics on the X4.

## Home and Apps

The default Home menu is:

- `Browse Files`
- `Apps`
- `Stats`
- `Sync Day`

`Lyra Custom` is the default theme for new installs.

Shortcuts can be managed from:

- `Settings > Apps > Shortcuts > Visibility Home and Apps`
- `Settings > Apps > Shortcuts > Order Home shortcuts`
- `Settings > Apps > Shortcuts > Order Apps shortcuts`

Default shortcut placement:

- `Home`: `Browse Files`, `Stats`, `Sync Day`
- `Apps`: `Settings`, `Reading Stats`, `Reading Heatmap`, `Reading Timeline`, `Achievements`, `Recent Books`, `Bookmarks`, `File Transfer`, `Sleep`

`Apps` always remains available in `Home`, but it can be moved to a different position.

## Sync Day and date model

This part matters, because several fork features depend on it.

The Xteink X4 should **not** be treated as if it had a reliable real-time clock that survives sleep in a trustworthy way.
So this fork uses a practical model:

1. `Sync Day` connects over Wi-Fi and fetches the current date/time using NTP.
2. That date becomes the valid reference for stats.
3. If the device later loses a valid current clock, the firmware falls back to the **last saved valid date**.
4. When the header is showing fallback date instead of a fresh synced date, it is marked with `!`.

Practical meaning:

- one sync per day before reading is usually enough
- after that, stats continue using the last valid saved day
- if the real day changes, you should sync again

`Sync Day` also shows diagnostics such as:

- `Clock valid`
- `Time source`
- `Current clock`
- `Synced this boot`
- `Header date`
- `Fallback date`

## Reading analytics suite

All reading analytics features share the same data source.

That means these views stay coherent with each other:

- `Reading Stats`
- `Reading Heatmap`
- `Reading Day`
- `Reading Timeline`
- per-book stats detail

### What gets tracked

- started books
- finished books
- total reading time
- daily reading time
- `7D` and `30D`
- goal streak
- max goal streak
- sessions
- per-book progress
- last read date
- current chapter where available

### Important rules

- a reading session counts when it reaches at least `1 minute`
- `Goal Streak` depends on whether you completed the `Daily Goal`
- `Reading Day` filters out books with less than `3 minutes` on that day
- books inside `/ignore_stats/` and its subdirectories are excluded from stats, sessions, heatmap, timeline and achievement tracking

### Main views

| View | Purpose |
|---|---|
| `Reading Stats` | main analytics hub with goal, streak, totals and started books |
| `More Details` | wider trends and graphs |
| `Reading Heatmap` | monthly calendar of reading intensity |
| `Reading Day` | one-day detail view opened from the heatmap |
| `Reading Timeline` | recent reading history by day |
| `Per-book stats detail` | cover, progress, sessions, time and last read info for a single book |

`Show after reading` can automatically open per-book stats detail when you exit a book, but only if the reading session was long enough to count as a real session.

## Achievements

`Achievements` adds a lightweight console-style progression layer on top of the same reading data already used by stats.

It provides:

- a dedicated `Apps > Achievements` screen
- locked vs unlocked states
- progress labels for cumulative milestones
- optional unlock popups
- reset support from `Settings > Apps`
- retroactive unlock sync from previous stats

Current achievement list:

- `Open Sesame` - start your first book
- `Collector` - start 5 different books
- `Shelf Diver` - start 10 different books
- `Warm-Up` - complete your first counted session
- `Page Ritual` - complete 10 counted sessions
- `Session Machine` - complete 25 counted sessions
- `Unstoppable` - complete 50 counted sessions
- `The End` - finish your first book
- `Trilogy` - finish 3 books
- `One-Hour Club` - read for 1 hour in total
- `Five and Rising` - read for 5 hours in total
- `Tenacious Reader` - read for 10 hours in total
- `Day Tripper` - read for 24 hours in total
- `Century Reader` - read for 100 hours in total
- `Goal Getter` - reach the daily goal once
- `Goal Habit` - reach the daily goal on 7 different days
- `Three in a Row` - reach a 3-day goal streak
- `Week Locked` - reach a 7-day goal streak
- `Pin It` - add your first bookmark
- `Bookmark Hoarder` - add 10 bookmarks
- `Marathon` - complete a 30-minute session

Important behavior:

- achievements are derived from tracked reading data
- on first use, existing stats can unlock achievements retroactively
- `Sync with prev. stats` can run that retroactive check again later
- `Reset achievements` clears achievement progress only, not reading stats

## Bookmarks

Bookmarks are implemented for EPUB.

There are two ways to use them:

- from inside a book
- from the global `Apps > Bookmarks` app

Inside EPUB reading:

- long press `Select` to add or remove a bookmark
- use the reader menu to open bookmarks
- jump directly to a saved bookmark

Global bookmarks app:

- lists books that contain bookmarks
- opening a book shows its saved bookmarks
- selecting one opens the EPUB directly at that location
- long press on a bookmark deletes just that bookmark
- long press on a book deletes all its bookmarks

All destructive actions ask for confirmation first.

## Sleep

The `Sleep` app makes custom sleep images easier to manage.

It can:

- find valid sleep folders
- preview images
- move between images
- choose the active folder
- switch between `Shuffle` and `Sequential`

Supported folder names:

- `sleep`
- `sleep_*`

## Settings

The most important fork-specific options live in:

- `Settings > Apps`

Main options:

| Area | Options |
|---|---|
| Date | `Display Day`, `Auto Sync Day`, `Date Format`, `Time Zone` |
| Reading stats | `Show after reading`, `Reset Reading Stats`, `Export Reading Stats`, `Import Reading Stats` |
| Achievements | `Enable achievements`, `Achievement popups`, `Reset achievements`, `Sync with prev. stats` |
| Navigation | `Shortcuts`, `Visibility Home and Apps`, `Order Home shortcuts`, `Order Apps shortcuts` |

## What requires Sync Day

Anything tied to day-level analytics depends on having a valid date reference.

That includes:

- daily goal
- goal streak
- max goal streak
- heatmap
- timeline
- `today`
- `7D`
- `30D`
- last read date

Recommended rule:

- do `Sync Day` once before reading each day

## Data persistence

This fork does **not** use a database.

It stores user state and reading analytics using files on the SD card, mainly in `/.crosspoint/`.

Important files include:

- `/.crosspoint/state.json`
- `/.crosspoint/reading_stats.json`
- `/.crosspoint/achievements.json`
- per-book `bookmarks.bin` files inside the EPUB cache path
- `/exports/*.json` for reading stats export files

## Versioning

Each firmware build exposes two identifiers:

- `version`: the human-readable release line, for example `1.1.2-vcodex`
- `version code`: a numeric build identifier, currently `2026032902`

The boot screen shows both values, so you can identify exactly which firmware is installed on the device.
For a brief release history, see [CHANGELOG.md](./CHANGELOG.md).

## Build from source

For normal device use, build or flash:

```sh
pio run -e vcodex_release
```

The resulting firmware is:

```text
.pio/build/vcodex_release/firmware.bin
```

Development prerequisites:

- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- USB-C cable
- Xteink X4

Clone and build:

```sh
git clone --recursive <your-fork-url>
cd crosspoint-vcodex
pio run -e vcodex_release
```

---

`crosspoint-vcodex` keeps the strong CrossPoint Reader base, but turns it into a more complete reading product for people who care about habit tracking, progress visibility, and practical reader UX.
