# Build And Release

Read this before changing build scripts, version strings, GitHub Actions,
release packaging, or firmware budget checks.

## Build Environments

CPR-vCodex currently ships one ESP32-C3 firmware binary for Xteink X4/X3 from
each tag. X4 Pro distribution is withdrawn because USB-locked units do not
have a confirmed recovery path. Its environments remain for maintainer-only
compile testing and must not be published.

| Purpose | ESP32-C3 (X4/X3) | ESP32-S3 (X4 Pro) |
|---|---|---|
| Development, serial logging | `default` | `x4pro` |
| Production release | `gh_release` | Internal only; do not distribute |
| Release candidate | `gh_release_rc` | Internal only; do not distribute |
| Smaller local profile | `slim` | - |

The C3 envs link against `partitions.csv`, whose OTA app slots are 0x640000
(6,553,600 bytes). That is the real slot on the X4 (the X3 slot is 0x770000).
The `x4pro*` envs link against `partitions_x4pro.csv`, which mirrors the stock
X4 Pro table (0x7E0000 = 8,257,536-byte slots), so PlatformIO reports the real
slot for both families. The X4 Pro envs also enable upstream's wolfSSL TLS 1.3
stack (`-DFREEINK_NET_WOLFSSL=1`, SecureNet, `patch_wolfssl.py`); the C3 envs
stay on the core mbedTLS because wolfSSL does not fit their slot.

Common commands:

```bash
pio run -e default
pio run -e gh_release
pio run -e x4pro
pio run -t clean
pio check
```

On constrained machines, use `-j 1` for lower memory pressure:

```bash
python -X utf8 -m platformio run -e default -j 1
```

## Versioning

- Base version lives in `platformio.ini` under `[crosspoint]`.
- `scripts/git_branch.py` writes version metadata to
  `artifacts/build-version.json` and C++ symbols to
  `src/version.generated.inc`.
- Runtime code should include `src/version.h` and read `CROSSPOINT_VERSION`
  from there. Avoid adding the version back to global `CPPDEFINES`; doing so
  makes every dev build look dirty to PlatformIO.
- Development builds include a `.devN-<sha>` suffix; `x4pro` dev builds also
  carry `-x4pro` in the version string.
- Release builds are tag-driven when `VCODEX_RELEASE_TAG` or `GITHUB_REF_NAME`
  matches `<base>.<release>-cpr-vcodex`.
- Local release counters under `artifacts/` are ignored by git. Only
  `gh_release` advances the counter and rewrites the README release row.

## Release Safety

Run pre-release checks before publishing. The default target is `gh_release`
and the check rejects stale X4 Pro browser artifacts:

```bash
python scripts/pre_release_check.py --tag 1.5.0.25-cpr-vcodex
```

The check builds `gh_release` as a dry run (`VCODEX_RELEASE_DRY_RUN=1`), parses
its Flash/RAM lines, writes a budget report, and validates:

- tag format and availability, release notes from `CHANGELOG.md`;
- `artifacts/<tag>.bin` + `<tag>.json` with `environment: gh_release`,
  `board: x4`, size <= 6,553,600;
- the auto-flash manifest contains X4/X3 only and no
  `docs/firmware/firmware-x4pro.bin` (see `autoflash-pages.md`).

`--env` remains available for maintainer diagnostics. Use `--skip-build` only
when the build artifacts already exist and were produced intentionally.

## Packaging Artifacts

Important scripts:

- `scripts/package_vcodex_bin.py`: packages firmware after PlatformIO builds.
  Internal envs starting with `x4pro` get the `-x4pro` artifact suffix and
  `board: "x4pro"` in metadata, but those files are not published.
- `scripts/firmware_budget_report.py`: reports flash usage and budget. The
  board profile comes from `--board`, else metadata `board`/`environment`, else
  a `-x4pro` tag suffix. Profiles: `x4` (slot 6,553,600) and `x4pro` (slot
  8,257,536, warning above 6,553,600). Default behaviour for the C3 artifact is
  unchanged.
- `scripts/pre_release_check.py`: release gate.
- `bin/build-vcodex.ps1`: Windows helper for local packaging.

Expected release assets (four per tag):

| ESP32-C3 (X4/X3) |
|---|
| `<tag>.bin` |
| `<tag>.json` |
| `<tag>-firmware-budget.json` |
| `<tag>-firmware-budget.md` |

Per-env budget:

- `gh_release`: flash budget percent applies to the 6,553,600 byte C3 slot.

## CI

GitHub Actions:

- `.github/workflows/ci.yml` runs formatting, `cppcheck`, unit tests, and both
  hardware builds. The X4 Pro build is compile-only; only the C3 artifact is
  uploaded.
- `.github/workflows/release.yml` runs on `*-cpr-vcodex` tags, builds
  `gh_release`, writes its budget report, validates its artifact pair, and
  uploads four C3 assets.
- `.github/workflows/pre_release_check.yml` is the manual dry run of
  `pre_release_check.py` for `gh_release` and uploads its build log and budget
  reports.
- `.github/workflows/sync_autoflash_firmware.yml` mirrors the published assets
  into `docs/firmware/` (see `autoflash-pages.md`).

Treat a green CI run as a baseline sanity check, not as proof that hardware
behavior is correct.
