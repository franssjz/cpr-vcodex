# Changelog

This changelog starts fresh for **CPR-vCodex Stats**, Daniel's unofficial
Codex-assisted fork focused on deeper reading statistics for the Xteink X4.

Older CPR-vCodex history is intentionally not copied here. That history belongs
to the upstream project. This file tracks what this fork changes from the point
where Daniel started publishing his own firmware builds.

## Unreleased

- No unreleased changes yet.

## Stats Preview 0.1.0

Published as GitHub release:

- [`1.2.0.44-cpr-vcodex`](https://github.com/danielc0603/cpr-vcodex-stats/releases/tag/1.2.0.44-cpr-vcodex)

Flashable firmware:

- `1.2.0.44-cpr-vcodex.bin`

### Project Identity

- Reframed the repository as **CPR-vCodex Stats**, an unofficial fork focused on
  reading statistics and analytics.
- Clarified that the fork is not affiliated with Xteink, CrossPoint Reader, or
  the upstream CPR-vCodex maintainer.
- Clarified that this is Codex-assisted firmware work.
- Added stronger use-at-your-own-risk language for custom firmware flashing.
- Documented that the current public release still uses inherited
  `1.2.0.<release>-cpr-vcodex` tags while the fork-facing release name starts
  at `Stats Preview 0.1.0`.

### Reading Stats Estimates

- Added a shared analytics model for time-left estimates.
- Added a **book time-left** estimate based on total recorded reading time and
  current book progress.
- Added a **chapter time-left** estimate based on time spent in the current
  chapter and progress made within that chapter.
- Kept estimates conservative: when there is not enough reading history or
  progress, the UI shows that more reading is needed before estimating.
- Rounded estimates to small readable time blocks instead of displaying false
  precision.
- Included session-count estimates when enough average-session data exists.

### Persisted Chapter Reading State

- Added per-book `currentChapterReadingMs`.
- Added per-book `chapterReadingStartProgressPercent`.
- Reset chapter estimate timing when the current chapter changes.
- Added chapter reading time to the existing reading session timing flow.
- Preserved backward compatibility with older stats files by defaulting missing
  new fields safely during import/load.

### Per-Book Stats Screen

- Replaced the previous single estimated-time-left card with two separate cards:
  **Book time left** and **Chapter time left**.
- Kept the existing progress bars and current-chapter display.
- Reused shared analytics helpers so future reading-stat screens can format
  estimates consistently.

### Extended Stats Screen

- Added **Avg Session**, calculated from total recorded reading time divided by
  counted sessions.
- Added **Avg Reading Day**, calculated from total recorded reading time divided
  by days with nonzero reading time.
- Adjusted the extended stats layout so the additional metrics fit above the
  existing recent-reading cards and charts.

### Browser Reading Stats Editor

- Updated `docs/reading-stats-editor/index.html` to preserve the new chapter
  estimate fields.
- Added editor fields for current chapter minutes and chapter start progress.
- Bumped exported reading stats format handling to version `6`.
- Kept older stats exports loadable by filling missing new fields with safe
  defaults.

### Release And Auto-Flash

- Built and published firmware release `1.2.0.44-cpr-vcodex`.
- Uploaded the flashable `.bin` firmware asset.
- Uploaded JSON and Markdown firmware budget reports.
- Synced `docs/firmware/firmware.bin` to the new release asset.
- Synced `docs/firmware/manifest.json` to point at the new fork release.
- Updated docs and browser-flasher metadata to show the new published firmware
  version.

### Verification

- Built the development firmware with:

```bash
pio run -e default
```

- Ran the release gate for the published tag with:

```bash
python3 scripts/pre_release_check.py --tag 1.2.0.44-cpr-vcodex --allow-existing-tag
```

- Release check passed with:
  - flash usage: `6123293 / 6553600 bytes`;
  - RAM usage: `102140 / 327680 bytes`;
  - packaged firmware size: `6135840 bytes`;
  - firmware SHA-256:
    `8d0e0e7a8ad7009b50fc152770de37cf1fb664e331a9839ab2c15774068ee9a1`.

### Known Notes

- The current firmware artifact name is still inherited:
  `1.2.0.44-cpr-vcodex.bin`.
- Future releases may move to a cleaner fork-specific naming scheme after the
  build scripts, release workflow, auto-flash sync, and validation scripts are
  updated together.
- The time-left estimates are only as good as the tracked reading history.
  Fresh installs and newly opened books may need more reading before estimates
  appear.
