---
name: cpr-release
description: Use when preparing CPR-vCodex releases, version bumps, GitHub tags, firmware artifacts, release notes, CI checks, GitHub Pages, browser auto-flash, docs/firmware/manifest.json, or docs/flash.html.
---

# CPR Release

Read `agent-docs/build-and-release.md` and `agent-docs/autoflash-pages.md`
before release work.

## Invariants

- Do not commit, tag, push, or publish unless the user explicitly asks.
- One tag publishes one binary: `<tag>.bin` (ESP32-C3, X4/X3, env
  `gh_release`). X4 Pro distribution is withdrawn; never publish or mirror a
  `-x4pro` binary unless that policy is explicitly reversed.
- The browser auto-flash flow must point to the latest published GitHub release
  asset, never an arbitrary local build. `docs/firmware/firmware-x4pro.bin`
  must not exist; the sync removes stale copies and the manifest must not
  contain `devices.x4pro`.
- Release documentation, manifests, Pages data, and GitHub release assets must
  agree on the same tag and firmware binaries.
- Keep release commands reproducible from the repository root.

## Workflow

1. Check `git status --short`, current branch, and recent tags before editing.
2. Update version strings, README, changelog, release notes, Pages manifest, and
   any release-facing docs requested by the user.
3. Build the published release environment:

```powershell
python -X utf8 -m platformio run -e gh_release -j 1
```

4. Run the release checker with the intended tag. It rebuilds `gh_release` as
   a dry run, writes `artifacts/<tag>-firmware-budget.{json,md}`, validates the
   artifact pair, and confirms Auto Flash contains no X4 Pro image:

```powershell
python -X utf8 scripts/pre_release_check.py --tag <tag>
```

5. If a GitHub release is created or updated, sync auto-flash metadata from the
   published release (fetches `<tag>.bin` and removes stale X4 Pro data):

```powershell
python -X utf8 scripts/sync_autoflash_firmware.py --repo franssjz/cpr-vcodex
```

6. Verify the GitHub release carries exactly four C3 assets (`<tag>.bin`,
   `.json`, `-firmware-budget.json`, `-firmware-budget.md`) and no `-x4pro`
   assets, plus CI status and Pages `flash.html`/manifest after publish (the
   manifest `devices` object should list only `x4` and `x3`). If Pages is stale, inspect the
   workflow rather than editing generated output by hand.

## Report Back

Include the final tag, C3 artifact path, its size against the 6,553,600-byte
slot, checks run, confirmation that no X4 Pro artifact was published, and
anything that still depends on GitHub Actions or Pages propagation.
