---
name: cpr-upstream-sync
description: Use when comparing or syncing CPR-vCodex with upstream crosspoint-reader, CrossInk, crosspet, papyrix, or other forks; cherry-picking changes; assessing fork divergence; or deciding whether a third-party feature should be ported.
---

# CPR Upstream Sync

Read `agent-docs/upstream-sync.md` before sync, fork comparison, or cherry-pick
work.

## Workflow

1. Identify the local repo, current branch, remotes, and working tree before any
   git operation.
2. Compare behavior and touched files before porting code. Prefer small,
   explainable changes over broad merges.
3. Preserve CPR-vCodex UX, release safety, stats behavior, and browser
   auto-flash invariants even when upstream or a fork differs.
4. When evaluating a third-party feature, look for:
   - memory and flash cost,
   - input/orientation behavior,
   - settings and i18n impact,
   - cache/data migration risk,
   - whether the feature still works without Wi-Fi.
5. Build with `default` after code syncs:

```powershell
python -X utf8 -m platformio run -e default -j 1
```

Use `gh_release` if the synced change affects release artifacts, binary size, or
production-only flags.

## Git Guardrails

- Do not assume remote names, branch names, or write permissions.
- Do not rewrite user work or revert unrelated changes.
- Document the upstream commit or fork commit that was analyzed or ported.
- If a GitHub fork reports "behind" because histories diverged, treat that as
  informational unless the user asks for history surgery.
