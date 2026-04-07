# Changelog

Brief firmware history for `cpr-vcodex`.

## 1.2.0.6

- bumped the fork release line to `1.2.0.6`
- refreshed README metadata and release references to match the current fork state
- kept the firmware feature set unchanged while publishing the updated release line

Version code: `2026040606`

## 1.2.0.5

- rebuilt the current firmware line as `1.2.0.5`
- restored `Lexend` as a reader font family after the later font-pruning pass removed it
- restored the translation files to a clean pre-corruption state after the mojibake regression introduced during the OTA/font refactor work

Version code: `2026040605`

## 1.2.0.4

- reviewed the translation set after the book-id migration work and repaired damaged strings in the language files
- normalized corrupted Hungarian and Lithuanian entries to clean text instead of leaving broken characters on device
- fixed the damaged Italian `ReadMe > Stats` body text
- added a global `Dark Mode` toggle in `Settings > Display`, implemented centrally in the renderer so UI text goes white-on-black while book images keep their original polarity
- added `Text Darkness` for anti-aliased reader text, adapted from the [`crosspet`](https://github.com/trilwu/crosspet) fork
- switched fork OTA metadata from the GitHub Releases API to a simple repo manifest (`ota.json`) to avoid device-side failures reaching `api.github.com`

Version code: `2026040504`

## 1.2.0.1

- rebased the fork onto the upstream `CrossPoint Reader 1.2.0` line
- separated base-version metadata from fork-version metadata in the firmware configuration
- switched the visible `vcodex_release` firmware line to `1.2.0.1` while keeping `CrossPoint Reader 1.2.0` as the tracked upstream base

Version code: `2026040401`

## 1.1.17-vcodex

- added long-press removal with confirmation for recent books in both `Home` and `Apps > Recent Books`
- kept recent-book removal limited to history only, without deleting the actual book file
- persisted recent-book removals cleanly through `/.crosspoint/recent.json`

Version code: `2026040201`

## 1.1.16-vcodex

- reduced sleep-screen flashing by switching the normal sleep rendering paths back to `HALF_REFRESH`
- kept `SleepScreenCache` in place so cached sleep images still load quickly
- refreshed the README header with the contributed text logo and tighter caption spacing

Version code: `2026040102`

## 1.1.15-vcodex

- improved `Reading Heatmap` month navigation so day selection can cross month boundaries naturally
- added direct month switching with the side `Up/Down` buttons in `Reading Heatmap`
- added goal-completion check marks inside heatmap day cells and refined their placement/size

Version code: `2026040101`

## 1.1.14-vcodex

- added `X Small` as a new reader font size with safe settings migration for existing installs
- regenerated the bundled boot/sleep logo header from the refreshed `Logo120.png`
- restored real bold and italic rendering for `Bookerly` when using `X Small`

Version code: `2026033102`

## 1.1.13-vcodex

- renamed the documented firmware line from `crosspoint-vcodex` to `cpr-vcodex`
- refreshed the main README intro to make the `CrossPoint Reader 1.1.1` base and the Codex-assisted evolution of the fork explicit
- bumped firmware release metadata to `1.1.13-vcodex`

Version code: `2026033101`

## 1.1.12-vcodex

- removed `Reading Timeline` as a separate app and folded the workflow around `Reading Stats`, `Reading Heatmap` and `Reading Day`
- split shortcut management into `Location Home and Apps` and a real `Visibility Home and Apps` with `Show / Hidden`
- fixed double-open behavior more robustly by swallowing the inherited `Confirm` release when a new activity becomes active

Version code: `2026033004`

## 1.1.10-vcodex

- improved stability around `Settings > Apps` actions by removing redundant settings saves from export/sync flows and reducing write pressure while reordering shortcuts
- reduced transient memory usage by writing more JSON stores directly to file instead of building intermediate strings in RAM
- optimized `Settings` and shortcut ordering flows with less repeated list rebuilding and lower allocation churn

Version code: `2026033002`

## 1.1.9-vcodex

- promoted the current firmware line to `1.1.9-vcodex`
- refreshed bundled documentation and release metadata to match the current firmware state

Version code: `2026033001`

## 1.1.8-vcodex

- rebuilt the achievements catalog with the new title set and many more finished-book milestones up to `100 books`
- moved achievements names and conditions to real i18n-backed strings instead of English/Spanish-only code paths
- regenerated the translation files from a clean base to fix corrupted language text such as the mojibake seen in `Languages`

Version code: `2026032908`

## 1.1.7-vcodex

- fixed `Achievements` title/description mismatches caused by definition lookup using enum order instead of the real achievement id
- restored coherence between progress targets and labels such as `Belle of the Books`, `Trilogy`, `Finish Line`, `Word Warden`, `Bookmark Hoarder` and `Flag Garden`

Version code: `2026032907`

## 1.1.6-vcodex

- fixed `Achievements` list navigation so the on-screen `Up/Down` controls actually scroll the achievements list
- expanded finished-book achievements with many more milestones, from early reading streaks to a `100 books` trophy line
- improved achievements ordering so milestone branches stay grouped more naturally in the list

Version code: `2026032906`

## 1.1.5-vcodex

- fixed `Achievements` opening behavior so it no longer auto-switches tabs on entry
- aligned `Achievements` navigation with `Settings`: `Confirm` switches tabs and `Up/Down` moves through the list
- expanded the achievements catalogue with more late-game milestones for books, sessions, goal days, streaks, bookmarks and long sessions

Version code: `2026032905`

## 1.1.4-vcodex

- redesigned `Achievements` with top tabs for `Pending` and `Completed`
- expanded the achievements catalogue with more session, books, goal-day, goal-streak and bookmark milestones
- added a stronger ladder of long-session trophies up to a real `Marathon`

Version code: `2026032904`

## 1.1.3-vcodex

- made `Daily Goal` configurable in `Settings > Apps` with `15 min / 30 min / 45 min / 60 min`
- aligned `Goal Streak`, Home stats shortcut, Reading Stats cards and Heatmap logic with the selected goal
- updated achievement goal-day and goal-streak progress so they stay coherent after changing the configured goal

Version code: `2026032903`

## 1.1.2-vcodex

- formalized firmware versioning and version code tracking
- added a concise changelog to make release-to-release changes easier to follow
- kept the current feature set stable as the new documented release line

Version code: `2026032902`

## 1.1.1-vcodex

- added `Achievements` with retroactive unlock bootstrap, popups and reset controls
- added `Sync with prev. stats` for achievements
- added shortcut visibility management and separate Home/Apps ordering
- added `/ignore_stats/` exclusion for stats, sessions, timeline, heatmap and achievement-related tracking
- polished Home/Apps navigation and fixed several app-entry double-open issues

Version code: `2026032901`

## 1.1.0-vcodex

- added `Reading Heatmap`, `Reading Day` and `Reading Timeline`
- expanded `Reading Stats` with richer per-book and aggregate views
- added `Sync Day` diagnostics, configurable date format and time zone selection
- improved Home with dynamic reading stats shortcut and configurable shortcut placement
- added a global `Bookmarks` app for EPUB bookmarks

## 1.0.0-vcodex

- established the fork identity and `Lyra Custom` default theme
- added the first `Sync Day + fallback` model for day-coherent reading analytics
- added the first wave of reading-focused UX improvements, custom sleep handling and stats workflows
- kept full CrossPoint Reader compatibility as the base firmware
