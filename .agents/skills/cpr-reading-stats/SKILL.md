---
name: cpr-reading-stats
description: Use when changing CPR-vCodex reading statistics, streaks, achievements, daily goals, manual reading-time corrections, import/export, Reading Stats screens, analytics, or the browser stats editor.
---

# CPR Reading Stats

Read `agent-docs/reading-stats.md` before changing stats storage, aggregation,
manual adjustments, streaks, achievements, import/export, on-device stats UI, or
the browser stats editor. If the change touches firmware UI or storage, also
read `agent-docs/firmware-constraints.md`.

## Workflow

1. Locate the source of truth for the stat being changed before altering UI.
2. Preserve trust: never create negative time/session values, never silently
   rewrite unrelated days/books, and avoid saves for tiny UI interactions.
3. Keep manual corrections explicit, reversible where practical, and tied to a
   selected book/date.
4. Ensure derived stats update consistently across:
   - per-book stats,
   - global stats,
   - streaks and achievements,
   - import/export,
   - browser editor views.
5. Keep device text in the i18n flow and keep the UI usable without comfortable
   text entry.

## Verification

Build firmware after device-side changes:

```powershell
python -X utf8 -m platformio run -e default -j 1
```

For stats migrations or import/export changes, inspect sample exported JSON/CSV
paths or tests before claiming compatibility. Report any scenario that still
requires device confirmation.
