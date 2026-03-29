# crosspoint-vcodex

`crosspoint-vcodex` is a feature-focused fork of **CrossPoint Reader** for the **Xteink X4**.

The base firmware is still CrossPoint Reader: open-source firmware for the X4 built with PlatformIO on **ESP32-C3**.
This fork keeps that foundation and adds a more complete day-to-day reading experience:

- better Home workflow
- configurable Home and Apps shortcuts
- richer reading analytics
- practical apps
- manual date sync for coherent stats
- global date format options
- customizable sleep screen browsing
- EPUB bookmarks with a global app

This project is **not affiliated with Xteink**.

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

## What crosspoint-vcodex adds

Compared with standard CrossPoint Reader, this fork adds or improves:

- `Lyra Custom` as the default UI theme
- a Home shortcut to `Stats`
- configurable shortcut placement between `Home` and `Apps`
- reorderable Home and Apps shortcut lists
- `Reading Stats` with started books, per-book detail, and extended stats
- `Reading Heatmap`
- `Reading Timeline`
- `Sync Day`
- global date format selection
- `Sleep` app with folder selection, preview, and sequential/shuffle browsing
- EPUB `Bookmarks` plus a global `Bookmarks` app
- `Settings > Apps` options for sync and stats workflows
- export/import/reset of reading stats
- per-book reading summaries after exiting a book if enabled

## Core CrossPoint Reader base

This fork is still based on the current CrossPoint Reader feature set, including:

- EPUB reading
- TXT reading
- XTC reading
- file browser
- Wi-Fi file transfer
- OTA update support
- KOReader Sync integration
- configurable fonts, layout and reading settings
- multilingual UI
- custom sleep screens

## Getting started in 5 minutes

If you just flashed the fork and want to use the main extras immediately:

1. Open `Home > Sync Day`
2. Connect to Wi-Fi and sync the date
3. Open a book and read normally
4. Check `Home > Stats` or `Apps > Reading Stats`
5. Open `Apps > Reading Heatmap` or `Apps > Reading Timeline` to review activity by day

That is enough to use the fork's main value: coherent day-based reading stats on the X4.

## Basic controls

The firmware keeps the standard Xteink X4 reading flow simple:

- bottom buttons are used for `Back`, `Confirm`, and front navigation
- side buttons are used for reading navigation
- `Confirm` usually opens or selects
- `Back` usually returns to the previous screen
- while reading, `Confirm` opens the reader menu
- long-press actions are used in a few places for destructive or secondary actions, such as deleting stats/bookmarks

In practice, the fork-specific areas you will use most are:

- `Home`
- `Apps`
- `Sync Day`
- `Reading Stats`
- `Reading Heatmap`
- `Reading Timeline`
- `Sleep`
- `Bookmarks`

## Why this fork exists

CrossPoint Reader already gives the X4 a strong open-source reading base.
`crosspoint-vcodex` pushes the firmware further toward a polished everyday reader:

- track reading in a way that actually feels useful
- make the Home screen more informative
- keep stats coherent even on a device without a reliable persistent clock
- turn extra features into real apps instead of hidden utilities

## Main experience changes

### Home

The Home screen is simplified and reading-first.

Current main menu order:

- `Browse Files`
- `Apps`
- `Stats`
- `Sync Day`

The Home screen also keeps:

- recent book covers at the top
- per-book progress under recent covers in `Lyra Custom`
- optional date in the header
- a compact stats subtitle under `Stats`

Home shortcuts are configurable:

- shortcuts can be assigned to `Home` or `Apps` in `Settings > Apps > Shortcuts`
- `Apps` always stays available in `Home`, but its position can be changed
- if more than 4 shortcuts are assigned to `Home`, the firmware switches to a paged `Shortcuts (x)` view

### Apps

`Apps` is the place for everything that does not need to stay in the main Home menu.

Default app list includes:

- `Settings`
- `Reading Stats`
- `Reading Heatmap`
- `Reading Timeline`
- `Recent Books`
- `Bookmarks`
- `File Transfer`
- `Sleep`

This list is also configurable from `Settings > Apps > Shortcuts`, and its order can be changed separately from Home.

## How date and time work

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

This is intentional. It is more honest and more useful than pretending the device has a perfectly persistent clock.

## Reading Stats suite

All reading analytics features share the same data source.

That means these views stay coherent with each other:

- `Reading Stats`
- `Reading Heatmap`
- `Reading Timeline`
- per-book stats detail

These all come from the same reading history, not separate stores.

### What gets tracked

Depending on your reading activity, the fork tracks:

- started books
- finished books
- total reading time
- daily reading time
- recent windows like `7D` and `30D`
- current goal streak
- max goal streak
- sessions
- per-book progress
- last read date
- current chapter where available

### Important rules

- a reading session counts when it reaches at least `1 minute`
- `Goal Streak` depends on whether you completed the `Daily Goal`
- `Reading Day` filters out books with less than `3 minutes` on that day
- stats can still be recorded on the last saved valid day even when the clock is no longer fresh, as long as you already used `Sync Day`

## Feature quick guide

### 1. Sync Day

Use this before reading if you want your stats to stay tied to the correct real day.

What it does:

- connects to Wi-Fi if needed
- synchronizes date and time using NTP
- stores the latest valid day for later fallback
- shows diagnostics so you know what the firmware is using

What you will see:

- `Device time`
- `How it works`
- `Diagnostics`

Diagnostics can show values such as:

- `Clock valid`
- `Time source`
- `Current clock`
- `Synced this boot`
- `Header date`
- `Fallback date`

Recommended use:

1. Open `Apps > Sync Day`
2. Connect to Wi-Fi if prompted
3. Press `Select` to sync
4. Confirm the date is correct
5. Read normally for the rest of the day

Recommended habit:

- do it once per day before reading
- do it again if the real day changed

### 2. Reading Stats

This is the main analytics hub.

It shows:

- daily goal progress
- goal streak
- max goal streak
- total reading time
- books finished
- books started
- started books list

Use it like this:

1. Open `Home > Stats` or `Apps > Reading Stats`
2. Review overall progress
3. Scroll through `Started Books`
4. Open a book to see per-book stats
5. Use `More Details` for wider trends and graphs

Extra actions:

- long press on a started book to delete that book's stats entry
- export/import/reset are available in `Settings > Apps`

### 3. Reading Heatmap

This is the calendar view of your reading.

It shows:

- a monthly grid
- day intensity by time read
- month total
- days read
- best day
- goal streak

Use it like this:

1. Open `Apps > Reading Heatmap`
2. Move through the month
3. Press `Select` on a day
4. Open that day's detail

It is useful for spotting:

- consistency
- missed days
- heavy reading days
- whether your daily reading habit is improving

### 4. Reading Day

This is the detail screen opened from the heatmap for a single date.

It shows:

- total reading time for that day
- how many books were meaningfully read that day
- the top book of that day
- the list of books read that day

Use it like this:

1. Open `Reading Heatmap`
2. Select a day
3. Review the books read that day
4. Open a book entry to jump into its stats detail

### 5. Reading Timeline

This is a recent-history view by day.

It is good when you want a quick answer to:

- what did I read yesterday?
- what have I been reading this week?
- which book dominated a given day?

Use it like this:

1. Open `Apps > Reading Timeline`
2. Browse recent days
3. Press `Select` on a day
4. Open the `Reading Day` detail for that date

### 6. Per-book stats detail

Each started book can open its own detail view.

This view can show:

- cover
- title and author
- book progress
- chapter progress
- total time
- last session
- sessions
- status
- last read date
- current chapter where available

Navigation:

- `Open` opens the actual book
- `Up/Down` can move through started books

### 7. Show after reading

This is controlled in:

- `Settings > Apps > Show after reading`

If enabled:

- when you exit a book, the firmware opens that book's stats detail automatically only if the reading session was long enough to count as a real session

This is useful if you want a lightweight post-reading summary without a separate recap screen.

### 8. Sleep

The `Sleep` app makes custom sleep images much easier to manage.

It can:

- find valid sleep folders
- preview images
- move between images
- choose the active folder
- switch between `Shuffle` and `Sequential`

Supported folder names:

- `sleep`
- `sleep_*`

Use it like this:

1. Open `Apps > Sleep`
2. Choose a valid folder
3. Preview images
4. Set the folder you want
5. Choose sequential or shuffle mode

### 9. Bookmarks

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

### 10. Recent Books

`Recent Books` was moved into `Apps`.

Use it like this:

1. Open `Apps > Recent Books`
2. Browse the recently opened books
3. Open one directly

### 11. File Transfer

`File Transfer` also lives in `Apps`.

Use it like this:

1. Open `Apps > File Transfer`
2. Connect to Wi-Fi if needed
3. Upload books over the local web interface

### 12. Settings

`Settings` is available inside `Apps` as the first item.

The most important fork-specific options are under:

- `Settings > Apps`

There you will find:

- `Display Day`
- `Auto Sync Day`
- `Date Format`
- `Time Zone`
- `Show after reading`
- `Shortcuts`
- `Order Home shortcuts`
- `Order Apps shortcuts`
- `Reset Reading Stats`
- `Export Reading Stats`
- `Import Reading Stats`

### 13. Shortcuts

Shortcut management lives in:

- `Settings > Apps > Shortcuts`

Each shortcut can be assigned to:

- `Home`
- `Apps`

Default layout:

- `Browse Files`, `Stats`, `Sync Day` in `Home`
- `Settings`, `Reading Stats`, `Reading Heatmap`, `Reading Timeline`, `Recent Books`, `Bookmarks`, `File Transfer`, `Sleep` in `Apps`

Reordering lives in:

- `Settings > Apps > Order Home shortcuts`
- `Settings > Apps > Order Apps shortcuts`

Use it like this:

1. Open `Settings > Apps`
2. Decide which shortcuts belong in `Home` or `Apps`
3. Open the order screen for the group you want
4. Press `Select` to enter move mode
5. Use `Up/Down` to move the selected shortcut
6. Press `Select` again to finish

`Apps` always stays available in `Home`, but it can still be moved to a different position.

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

After that:

- the firmware can continue attributing reading to the last saved valid day
- but if the real day changes, sync again

## Data persistence

This fork does **not** use a database.

It stores user state and reading analytics using files on the SD card, mainly in `/.crosspoint/`.

Important files include:

- `/.crosspoint/state.json`
- `/.crosspoint/reading_stats.json`
- `/.crosspoint/bookmarks.json` or per-book bookmark data depending on feature store
- `/exports/*.json` for reading stats export files

This means:

- stats can be exported
- stats can be imported
- the system is easy to back up

## Installation

### Recommended build target

For normal device use, build or flash:

```sh
pio run -e vcodex_release
```

The resulting firmware is:

```text
.pio/build/vcodex_release/firmware.bin
```

### Manual flashing

Connect the Xteink X4 by USB-C and use your usual flash workflow for CrossPoint-based firmware, or flash the built `firmware.bin` through your preferred method.

### Development

Prerequisites:

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

## Notes

- This fork is built for people who actually read on the X4 every day.
- `Sync Day` is not cosmetic; it is the anchor for coherent daily analytics.
- The firmware is intentionally honest about the X4 clock model instead of pretending sleep preserves a reliable real-time clock.

---

`crosspoint-vcodex` keeps the strong CrossPoint Reader base, but turns it into a more complete reading product for people who care about habit tracking, progress visibility, and practical reader UX.
