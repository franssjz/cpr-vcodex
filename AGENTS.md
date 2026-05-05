# Agent Guide for CPR-vCodex

CPR-vCodex is a fork of CrossPoint Reader for the Xteink X4 e-reader. Its main
value is a stable reading experience plus CPR-vCodex features such as reading
statistics, browser-based auto-flash, and release safety checks.

Use this file as the first map, then read only the task-specific documents that
matter for the current change.

## Task Documents

- `agent-docs/firmware-constraints.md`: ESP32-C3 limits, memory rules, C++ and UI safety.
- `agent-docs/build-and-release.md`: PlatformIO builds, versioning, releases, CI, artifacts.
- `agent-docs/autoflash-pages.md`: GitHub Pages, Web Serial flasher, release firmware sync.
- `agent-docs/reading-stats.md`: on-device reading analytics and browser stats editor.
- `agent-docs/upstream-sync.md`: keeping this fork aligned with upstream CrossPoint Reader.

## Always-On Rules

- Preserve stability over feature size. The ESP32-C3 has about 380 KB usable RAM
  and no PSRAM.
- Prefer existing project patterns and nearby code over new abstractions.
- Before changing firmware behavior, inspect the relevant files with `rg` and
  cite exact paths in your reasoning.
- User-facing device text must use the `tr()` i18n flow. Logs may be plain text.
- Avoid new heap allocation in render loops, input loops, and parser hot paths.
- Do not edit generated outputs by hand when a generator exists.
- Do not use symlinks for `CLAUDE.md` or `AGENTS.md`; cross-platform checkout
  must remain safe.
- For releases, the auto-flash firmware must come from the latest published
  GitHub release asset, not from an arbitrary local build.

## Quick Commands

```bash
pio run -e default
pio run -e gh_release
python scripts/pre_release_check.py --tag 1.2.0.39-cpr-vcodex
python scripts/sync_autoflash_firmware.py --repo franssjz/cpr-vcodex
```

On Windows PowerShell, run the same commands from the repository root. If
Unicode output looks broken, set `PYTHONUTF8=1` or use `python -X utf8`.
