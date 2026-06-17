# Reading Stats

Read this before changing reading analytics, streaks, daily goals, import/export,
or the browser stats editor.

## Purpose

CPR-vCodex differentiates itself by helping users keep reading consistently.
Statistics should favor trust, low friction, and clear recovery over flashy
metrics.

## Key Firmware Files

- `src/ReadingStatsStore.cpp` and `.h`: persistence and data model.
- `src/util/ReadingStatsAnalytics.cpp` and `.h`: derived metrics.
- `src/activities/apps/ReadingStatsActivity.cpp`: main stats hub.
- `src/activities/apps/ReadingStatsExtendedActivity.cpp`: extended stats.
- `src/activities/apps/ReadingStatsDetailActivity.cpp`: per-book details.
- `src/activities/apps/ReadingDayDetailActivity.cpp`: per-day detail.
- `src/activities/apps/ReadingHeatmapActivity.cpp`: heatmap view.
- `src/activities/apps/ReadingProfileActivity.cpp`: profile view.
- `src/activities/reader/`: reader activities that tick sessions and progress.

## Browser Editor

- `docs/reading-stats-editor/index.html`: local browser editor for exported
  stats.
- Keep the editor offline-first. It should not upload private reading data.
- When changing export/import schema, preserve backward compatibility or add a
  clear migration path.

## Design Rules

- Do not save stats on every tiny interaction. Debounce or save on activity exit
  or meaningful session changes.
- Manual corrections should be explicit and reversible where possible.
- Preserve current user ability to set reading day manually without Wi-Fi.
- Prefer simple, inspectable stored data over clever derived state.
- If adding physical-book/manual sessions, mark them distinctly from device-read
  sessions so analytics can include or filter them later.

## Verification

Check at least:

- a normal EPUB/TXT reading session increments time;
- daily goal and streak are still correct across day boundaries;
- import/export round-trips;
- reset flows require confirmation;
- browser editor can open existing exported stats.
