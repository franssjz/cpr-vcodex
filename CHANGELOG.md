# Changelog

Brief firmware history for `crosspoint-vcodex`.

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
