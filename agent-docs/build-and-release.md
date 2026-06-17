# Build And Release

Read this before changing build scripts, version strings, GitHub Actions,
release packaging, or firmware budget checks.

## Build Environments

- `default`: development firmware with serial logging.
- `gh_release`: production firmware, serial output disabled, event logs reduced.
- `gh_release_rc`: release candidate style build.
- `slim`: smaller local build profile.

Common commands:

```bash
pio run -e default
pio run -e gh_release
pio run -t clean
pio check
```

On constrained machines, use `-j 1` for lower memory pressure:

```bash
python -X utf8 -m platformio run -e default -j 1
```

## Versioning

- Base version lives in `platformio.ini` under `[crosspoint]`.
- `scripts/git_branch.py` injects `CROSSPOINT_VERSION`,
  `VCODEX_RELEASE_SEQ`, and build kind defines.
- Development builds include a `.devN-<sha>` suffix.
- Release builds are tag-driven when `VCODEX_RELEASE_TAG` or `GITHUB_REF_NAME`
  matches `<base>.<release>-cpr-vcodex`.
- Local release counters under `artifacts/` are ignored by git. Do not rely on
  them as the only source of published release truth.

## Release Safety

Run pre-release checks before publishing:

```bash
python scripts/pre_release_check.py --tag 1.2.0.39-cpr-vcodex
```

The check should verify tag format, release artifacts, firmware budget, and
auto-flash consistency. Use `--skip-build` only when the build artifacts already
exist and were produced intentionally.

## Packaging Artifacts

Important scripts:

- `scripts/package_vcodex_bin.py`: packages firmware after PlatformIO builds.
- `scripts/firmware_budget_report.py`: reports flash usage and budget.
- `scripts/pre_release_check.py`: release gate.
- `bin/build-vcodex.ps1`: Windows helper for local packaging.

Expected release assets:

- `<tag>.bin`
- `<tag>.json`
- `<tag>-firmware-budget.json`
- `<tag>-firmware-budget.md`

## CI

GitHub Actions runs `.github/workflows/ci.yml` on pushes. It currently checks:

- formatting for changed C/C++ files;
- `cppcheck` for high-severity defects;
- a firmware build;
- final aggregate test status.

Treat a green CI run as a baseline sanity check, not as proof that hardware
behavior is correct.
