---
name: cpr-release
description: Use when preparing CPR-vCodex releases, version bumps, GitHub tags, firmware artifacts, release notes, CI checks, GitHub Pages, browser auto-flash, docs/firmware/manifest.json, or docs/flash.html.
---

# CPR Release

Read `agent-docs/build-and-release.md` and `agent-docs/autoflash-pages.md`
before release work.

## Invariants

- Do not commit, tag, push, or publish unless the user explicitly asks.
- The browser auto-flash flow must point to the latest published GitHub release
  asset, never an arbitrary local build.
- Release documentation, manifests, Pages data, and GitHub release assets must
  agree on the same tag and firmware binary.
- Keep release commands reproducible from the repository root.

## Workflow

1. Check `git status --short`, current branch, and recent tags before editing.
2. Update version strings, README, changelog, release notes, Pages manifest, and
   any release-facing docs requested by the user.
3. Build release firmware with:

```powershell
python -X utf8 -m platformio run -e gh_release -j 1
```

4. Run the release checker with the intended tag:

```powershell
python -X utf8 scripts/pre_release_check.py --tag <tag>
```

5. If a GitHub release is created or updated, sync auto-flash metadata from the
   published release:

```powershell
python -X utf8 scripts/sync_autoflash_firmware.py --repo franssjz/cpr-vcodex
```

6. Verify the GitHub release, CI status, and Pages `flash.html`/manifest after
   publish. If Pages is stale, inspect the workflow rather than editing generated
   output by hand.

## Report Back

Include the final tag, artifact path, firmware size/budget if measured, checks
run, and anything that still depends on GitHub Actions or Pages propagation.
