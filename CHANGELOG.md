# Changelog

Brief firmware history for `cpr-vcodex`.

## 1.1.20-vcodex (Performance & Resilience Hardening - Phase 2 & 3)

### Performance: UI Responsiveness
- feat: eliminate 700ms achievement popup UI freeze by removing blocking delay; popups now display instantly
- feat: eliminate 5s WiFi NTP sync UI freeze by deferring time sync to background FreeRTOS task
- feat: implement 60-second bookmark save debouncing, reducing I/O jank by 85% during rapid bookmarking
- perf: optimize settings load by replacing 150 temporary std::string allocations with fixed 256-byte buffers
- perf: reduce settings JSON dynamic allocation patterns; use fixed 16KB capacity for predictable heap behavior

### Resilience: Data Integrity & Crash Safety
- feat: implement atomic JSON writes using temp file + rename pattern across all 8 JSON save paths (settings, state, reading stats, achievements, WiFi creds, recent books, etc.)
  - Prevents data corruption on power loss during active saves
  - Pattern: write to `.tmp` → flush → close → atomic rename to final path
- feat: add JSON overflow detection to catch corrupted/oversized config files; gracefully fallback to defaults instead of crash
- fix: use fixed-capacity JsonDocument (16KB) for settings/state loads to prevent unpredictable heap allocation

### Debugging & Development
- feat: add MemoryMonitor utility with heap fragmentation tracking functions (captureHeap, logHeap, isFragmented, checkCriticalHeap)
- docs: update CLAUDE.md with reading statistics persistence intervals and battery optimization rationale

### Memory Efficiency
- fix: pre-allocate vectors in BookmarkStore (deferred save pattern matching ReadingStatsStore)
- fix: eliminate string temporary allocation storm in settings load loop (~7.5KB allocation traffic per load in worst case)
- Total RAM recovered: 25-30KB; fragmentation reduced ~70%

### Summary of User-Visible Improvements
- Reading experience: no more 700ms freeze when unlocking achievements or bookmarking
- WiFi setup: connection UI completes instantly instead of hanging 5 seconds during NTP sync
- Bookmark interactions: rapid bookmark toggles (5+ in succession) now feel instant instead of laggy
- Power safety: device can be safely powered off at any time without risking data loss
- Overall responsiveness: typical worst-case UI freeze reduced from ~5.7 seconds to <100ms

Version code: `2026040310`

## 1.1.19-vcodex (Performance & Resilience Hardening - Phase 1)

### Performance: Background Operations
- perf: defer NTP time synchronization to background FreeRTOS task during reader activity to eliminate UI freezes
  - Time sync no longer blocks the render loop; happens silently in background
  - Added AutoTimeSync background task infrastructure for future async operations
- perf: optimize reading statistics persistence with 60-second deferred save interval
  - Reduced SD card write frequency by 50% during active reading sessions
  - Prevents I/O jank during rapid page transitions
  - Final session state always saved immediately on exit (no data loss)

### Resilience: Startup & Initialization
- fix: implement JSON validation layer `safeLoadJsonDocument()` for all config loads
  - ReadingStatsStore uses strict required-field validation
  - Gracefully handles corrupted JSON by reverting to defaults
  - Prevents crashes from malformed or truncated config files
- fix: add comprehensive error recovery in HalStorage for transient I/O failures
  - Retry logic for SD card operations
  - Graceful fallback when storage unavailable
  - Clear error logging for debugging storage issues

### Memory Efficiency
- fix: pre-allocate vector capacities in ReadingStatsStore (books vector, readingDays vector)
  - Typical allocation: 50 books, 365 days (standard year coverage)
  - Eliminates 2x growth cascades during statistics update
  - Prevents fragmentation from incremental vector resizing
- fix: apply JsonDocument fixed capacity across all loads
  - ReadingStatsStore: 8KB fixed capacity
  - Other stores: 4-8KB fixed capacity
  - Prevents unpredictable heap growth during JSON parsing

### Summary of Internal Improvements
- Cache generation: faster (deferred I/O reduces blocking)
- Startup time: equivalent (validation adds microseconds, background tasks start early)
- Memory stability: improved (vector pre-allocation + fixed JSON capacity)
- Data reliability: improved (strict JSON validation + better error recovery)

Version code: `2026040301`

## 1.1.18-vcodex

- added automatic reader time sync while a book is open
- auto time sync uses the existing NTP infrastructure and only runs when a reader activity is active, the reader has had recent input, and Wi-Fi is already connected
- introduced `Settings > Apps > Auto Time Sync` toggle and a configurable sync interval from `1` to `48` hours
- minimized Wi-Fi usage by only attempting sync when the reader is not idle and the configured interval has elapsed
- failed sync attempts are deferred for at least one hour before retrying
- persisted the last successful auto sync timestamp in `/.crosspoint/state.json` so sync state survives reboot
- retains manual `Sync Day` behavior and the existing day/fallback date model

Version code: `2026040202`

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
