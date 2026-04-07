
<p align="center">
  <img src="./docs/images/logotext_by_Which-Estimate4566.svg" alt="cpr-vcodex logo" width="720" />
  <br />
  <sub>Logo contributed by Which-Estimate4566.</sub>
</p>

## Screenshots

<p align="center">
  <img src="./docs/images/screenshots.png" alt="cpr-vcodex overview" width="1000" />
</p>

# cpr-vcodex

## At a glance

| Item | Value |
|---|---|
| Base firmware | `CrossPoint Reader 1.2.0` |
| Upstream base commit | [`e6c6e72`](https://github.com/crosspoint-reader/crosspoint-reader/commit/e6c6e72a249a0edcd525ee29029739e457e4e797) |
| Upstream carry-forward | manual carry-forward from [`9b38851`](https://github.com/crosspoint-reader/crosspoint-reader/commit/9b388513869a16f8bb3310fc5a3dd85385614fac), [`1c13331`](https://github.com/crosspoint-reader/crosspoint-reader/commit/1c1333118962f456ff56a3c599f52520d2d204d9), [`11984f8`](https://github.com/crosspoint-reader/crosspoint-reader/commit/11984f8fefc308c57a32d580b47d14b9a53e5621), [`f429f90`](https://github.com/crosspoint-reader/crosspoint-reader/commit/f429f9035c7301636c94b201bf27962d0289fabf), [`fa3c7d9`](https://github.com/crosspoint-reader/crosspoint-reader/commit/fa3c7d96a0020687e1e22dd720e285402987d99b) and [`cff3e12`](https://github.com/crosspoint-reader/crosspoint-reader/commit/cff3e12a0ac2bd496cc23588e9fc0b49edfd98d0) |
| Device | Xteink X4 |
| Current release (vCodex) | `1.2.0.7` |
| Version code | `2026040707` |
| Release notes | [CHANGELOG.md](./CHANGELOG.md) |
| Recommended install | browser fast flash |

`cpr-vcodex` builds on the great work of **CrossPoint Reader**. The current firmware line is now based on **CrossPoint Reader 1.2.0**, and this fork has been extended and improved with the help of **OpenAI Codex** for the **Xteink X4**.

In practical terms, the fork currently takes **CrossPoint Reader `1.2.0` / commit `e6c6e72`** as its upstream base and now manually carries forward the upstream work from **`9b38851`**, **`1c13331`**, **`11984f8`**, **`f429f90`**, **`fa3c7d9`** and **`cff3e12`**, adapted carefully to keep the fork stable on the **Xteink X4**.

This project is **not affiliated with Xteink**.

## Highlights

- richer reading analytics on the device itself: `Reading Stats`, `Heatmap`, `Reading Day` and per-book detail
- console-style `Achievements` built on top of the same reading data
- better day/date model through `Sync Day`, so stats stay coherent on the X4
- faster-to-use `Home` and `Apps` with configurable shortcut placement and order
- reader-focused extras like `Dark Mode`, `Text Darkness`, `Lexend` and EPUB bookmarks
- practical device-side utilities such as `ReadMe`, `If found, please return me` and `Sleep` tools

## Easy installation

For most users, this is the easiest way to install the firmware:

1. Download the latest `cpr-vcodex` firmware from [GitHub Releases](https://github.com/franssjz/cpr-vcodex/releases).
2. Turn on and unlock your Xteink X4.
3. Open [xteink.dve.al](https://xteink.dve.al/).
4. In `OTA fast flash controls`, select the downloaded firmware file.
5. Click `Flash firmware from file`.
6. Select the device when the browser asks.
7. Wait for the installation to finish.
8. Restart the device:
   press the bottom-right button once, then press and hold the right power button.

> To return to the original CrossPoint Reader later, repeat the same process with the original firmware file.

The in-device update entry is currently hidden.
For now, update using the release file from GitHub and the browser flasher above.

## What this fork adds

| Feature | What it adds | More info |
|---|---|---|
| `Reading Stats` | richer totals, started books, per-book detail and extended trends | [Reading analytics suite](#reading-analytics-suite) |
| `Reading Heatmap` | monthly calendar view of reading intensity | [Reading analytics suite](#reading-analytics-suite) |
| `Reading Day` | drill-down into one specific reading day | [Reading analytics suite](#reading-analytics-suite) |
| `Achievements` | console-style milestones and optional popups | [Achievements](#achievements) |
| `Sync Day` | manual Wi-Fi day sync plus fallback-day logic | [Sync Day and date model](#sync-day-and-date-model) |
| `Home + Shortcuts` | configurable Home/Apps placement and reorderable shortcut lists | [Home and Apps](#home-and-apps) |
| `Dark Mode` | global white-on-black UI and reader rendering toggle | [Settings](#settings) |
| `Bookmarks` | EPUB bookmarks plus a global bookmarks app | [Bookmarks](#bookmarks) |
| `Lyra Custom` default theme | reading-first default presentation from first boot | [Home and Apps](#home-and-apps) |
| `ReadMe` | on-device quick guide for the main CPR-vCodex features | [ReadMe](#readme) |
| `If found, please return me` | lost-device contact screen fed by `/if_found.txt` on the SD card | [If found, please return me](#if-found-please-return-me) |
| `Sleep tools` | folder selection, preview and sequential/shuffle behavior | [Sleep](#sleep) |
| `Text Darkness` | darker anti-aliased reader text, adapted from the [`crosspet`](https://github.com/trilwu/crosspet) fork | [Settings](#settings) |
| `Lexend reader font` | additional reader font family adapted from the [`crosspet`](https://github.com/trilwu/crosspet) fork | [Settings](#settings) |
| `Configurable Daily Goal` | choose `15 / 30 / 45 / 60 min` and use that target for goal-based stats | [Reading analytics suite](#reading-analytics-suite) |
| `Date controls` | global date format and time zone settings | [Settings](#settings) |

## 5-minute start

If you just flashed the fork and want the main extras immediately:

1. Open `Home > Sync Day`
2. Connect to Wi-Fi and sync the date
3. Open a book and read normally
4. Open `Home > Stats` or `Apps > Reading Stats`
5. Open `Apps > Reading Heatmap`

That is enough to start using the core value of the fork: coherent day-based reading analytics on the X4.

## Home and Apps

The default Home menu is:

- `Browse Files`
- `Apps`
- `Stats`
- `Sync Day`

`Lyra Custom` is the default theme for new installs.

Shortcuts can be managed from:

- `Settings > Apps > Shortcuts > Location Home and Apps`
- `Settings > Apps > Shortcuts > Visibility Home and Apps`
- `Settings > Apps > Shortcuts > Order Home shortcuts`
- `Settings > Apps > Shortcuts > Order Apps shortcuts`

Default shortcut placement:

- `Home`: `Browse Files`, `Stats`, `Sync Day`
- `Apps`: `Settings`, `Reading Stats`, `Reading Heatmap`, `Achievements`, `If found, please return me`, `ReadMe`, `Recent Books`, `Bookmarks`, `File Transfer`, `Sleep`

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

By default the time zone is `Spain / Madrid`.
You can select your own `Time Zone` and `Date Format` from `Settings > Apps`.

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

- a reading session counts when it reaches at least `3 minutes`
- `Daily Goal` is configurable to `15 / 30 / 45 / 60 min`
- `Goal Streak` depends on whether you completed the configured `Daily Goal`
- `Reading Day` filters out books with less than `3 minutes` on that day
- books inside `/ignore_stats/` and its subdirectories are excluded from stats, sessions, heatmap and achievement tracking

### Main views

| View | Purpose |
|---|---|
| `Reading Stats` | main analytics hub with goal, streak, totals and started books |
| `More Details` | wider trends and graphs |
| `Reading Heatmap` | monthly calendar of reading intensity |
| `Reading Day` | one-day detail view opened from the heatmap |
| `Per-book stats detail` | cover, progress, sessions, time and last read info for a single book |

`Show after reading` can automatically open per-book stats detail when you exit a book, but only if the reading session was long enough to count as a real session.

## Achievements

`Achievements` adds a lightweight console-style progression layer on top of the same reading data already used by stats.

It provides:

- a dedicated `Apps > Achievements` screen
- top tabs for `Pending` and `Completed`
- `Settings`-style controls: `Confirm` switches tabs, `Up/Down` moves through the list
- locked vs unlocked states
- progress labels for cumulative milestones
- optional unlock popups
- reset support from `Settings > Apps`
- retroactive unlock sync from previous stats

Current achievement list:

Current catalog: `62 achievements`

### Started books

| Title | Unlock condition |
|---|---|
| `Open Sesame` | start your first book |
| `My Precious` | start 5 different books |
| `I Volunteer as Tribute` | start 10 different books |
| `Through the Wardrobe` | start 25 different books |
| `So Many Beginnings` | start 50 different books |

### Counted sessions

| Title | Unlock condition |
|---|---|
| `Once Upon a Time` | complete your first counted session |
| `Just One More Page` | complete 10 counted sessions |
| `Hold the Book!` | complete 25 counted sessions |
| `Mischief Managed` | complete 50 counted sessions |
| `The Reading Stone` | complete 100 counted sessions |
| `The NeverEnding Story` | complete 200 counted sessions |

### Finished books

| Title | Unlock condition |
|---|---|
| `It's leviOsa, not levioSA!` | finish your first book |
| `The Two Towers` | finish 2 books |
| `Trilogy` | finish 3 books |
| `Hermione Granger` | finish 5 books |
| `Jane Eyre` | finish 7 books |
| `Lizzy Bennet` | finish 10 books |
| `Belle's Library` | finish 15 books |
| `Matilda` | finish 20 books |
| `The NeverEnding Story` | finish 25 books |
| `The Book Was Better` | finish 30 books |
| `One More Chapter` | finish 35 books |
| `Gandalf the White` | finish 40 books |
| `The Book Thief` | finish 45 books |
| `Read, Set, Go!` | finish 50 books |
| `One Read to Rule Them All` | finish 55 books |
| `The Library of Alexandria` | finish 60 books |
| `Dorian Read` | finish 65 books |
| `The Name of the Page` | finish 70 books |
| `Moony, Wormtail, Padfoot & Read` | finish 75 books |
| `The Shining` | finish 80 books |
| `The Ultimate Plot Twist` | finish 85 books |
| `I, Reader` | finish 90 books |
| `Robot Dreams` | finish 95 books |
| `The Pagemaster` | finish 100 books |

### Total reading time

| Title | Unlock condition |
|---|---|
| `One Hour Later` | read for 1 hour in total |
| `An Unexpected Journey` | read for 5 hours in total |
| `The Fellowship of the Read` | read for 10 hours in total |
| `A Day in Pages` | read for 24 hours in total |
| `Halfway to Narnia` | read for 50 hours in total |
| `Read and Prejudice` | read for 100 hours in total |
| `Wonderland` | read for 200 hours in total |

### Goal days and streaks

| Title | Unlock condition |
|---|---|
| `Mission Accomplished` | reach the daily goal once |
| `Seven Kingdoms` | reach the daily goal on 7 different days |
| `The Month of Living Bookishly` | reach the daily goal on 30 different days |
| `Still Not a Muggle` | reach the daily goal on 60 different days |
| `Willy Fog` | reach the daily goal on 80 different days |
| `Not Today` | reach a 3-day goal streak |
| `White Rabbit` | reach a 7-day goal streak |
| `Windrunner` | reach a 14-day goal streak |
| `Stormblessed` | reach a 30-day goal streak |
| `Radiant` | reach a 60-day goal streak |

### Bookmarks

| Title | Unlock condition |
|---|---|
| `Marked for Later` | add your first bookmark |
| `The Usual Susmarks` | add 10 bookmarks |
| `The Bookmark Knight Rises` | add 25 bookmarks |
| `Too Many Tabs` | add 50 bookmarks |

### Long sessions

| Title | Unlock condition |
|---|---|
| `And So It Begins` | complete a 15-minute session |
| `One Does Not Simply Stop` | complete a 30-minute session |
| `Locked, Loaded, Booked` | complete a 45-minute session |
| `There and Back Again` | complete a 60-minute session |
| `The Long Read` | complete a 90-minute session |
| `Extended Edition` | complete a 120-minute session |

Important behavior:

- achievements are derived from tracked reading data
- on first use, existing stats can unlock achievements retroactively
- `Sync with prev. stats` can run that retroactive check again later
- `Reset achievements` clears achievement progress only, not reading stats

## ReadMe

`ReadMe` is a built-in on-device guide for the main vCodex features.

It includes quick pages for:

- `Sync Day`
- `Reading Stats`
- `Bookmarks`
- `Sleep`
- `Customize Home and Apps`
- `Achievements`
- `If found, please return me`

How it works:

- open `Apps > ReadMe`
- choose a topic from the list
- press `Open` to view the full explanation
- use `Up / Down` to scroll long pages
- press `Back` to return to the topic list

This is meant to give device-side help without needing to re-open GitHub every time.

## If found, please return me

This app is a simple lost-device return screen.

How it works:

- open `Apps > If found, please return me`
- the screen always shows a fixed intro message
- if `/if_found.txt` exists on the SD root, its content is shown below in bold
- if the file does not exist, the app shows a bold fallback message explaining how to create it
- long content can be scrolled with `Up / Down`

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
| Reader | `Text Anti-Aliasing`, `Text Darkness`, `Reader Font Family` with `Lexend` |
| Display | `Dark Mode`, `Sunlight Fading Fix`, `UI Theme`, sleep-screen controls |
| Date | `Display Day`, `Auto Sync Day`, `Date Format`, `Time Zone` |
| Reading stats | `Daily Goal`, `Show after reading`, `Reset Reading Stats`, `Export Reading Stats`, `Import Reading Stats` |
| Achievements | `Enable achievements`, `Achievement popups`, `Reset achievements`, `Sync with prev. stats` |
| Navigation | `Shortcuts`, `Location Home and Apps`, `Visibility Home and Apps`, `Order Home shortcuts`, `Order Apps shortcuts` |

`Text Darkness` is adapted from the [`crosspet`](https://github.com/trilwu/crosspet) fork. It only affects anti-aliased reader text and gives you `Normal`, `Dark` and `Extra Dark` rendering options.

`Dark Mode` adds a global white-on-black rendering toggle from `Settings > Display`. In this fork it is handled centrally in the renderer so menus and text switch cleanly to white-on-black while preserving normal image polarity instead of inverting book art and illustrations.

`Lexend` is also adapted from the [`crosspet`](https://github.com/trilwu/crosspet) fork as an extra reader font family. In this fork it bundles the regular face across the available sizes and uses it as the fallback for bold and italic variants to keep flash usage under control.

## What requires Sync Day

Anything tied to day-level analytics depends on having a valid date reference.

That includes:

- daily goal
- goal streak
- max goal streak
- heatmap
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
- `/.crosspoint/recent.json` for recent books history

## Versioning

Each firmware build now keeps the base project version and the fork release separate:

- `crosspoint.version`: the upstream base release, currently `1.2.0`
- `vcodex.version`: the fork release shown to the user, currently `1.2.0.7`
- `vcodex.version_code`: the exact build identifier, currently `2026040707`

The firmware UI keeps showing the fork version to avoid confusion, while the base version remains available as metadata for tracking upstream sync.
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
git clone --recursive https://github.com/franssjz/cpr-vcodex.git
cd cpr-vcodex
pio run -e vcodex_release
```

---

`cpr-vcodex` keeps the strong CrossPoint Reader base, but turns it into a more complete reading product for people who care about habit tracking, progress visibility, and practical reader UX.
