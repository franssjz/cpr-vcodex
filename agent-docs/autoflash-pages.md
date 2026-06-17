# Auto-Flash And GitHub Pages

Read this before changing `docs/flash.html`, browser flasher code, Pages
publishing, firmware manifests, or release sync.

## Invariant

The auto-flash page must flash the latest firmware published as a GitHub release
asset. It must not silently flash a random local `firmware.bin`.

The browser copy exists so GitHub Pages can serve it reliably, but its source of
truth is the latest release asset.

## Key Files

- `docs/flash.html`: user-facing browser flasher page.
- `docs/assets/site.js`: shared site text and flasher UI behavior.
- `docs/firmware/manifest.json`: current auto-flash release metadata.
- `docs/firmware/firmware.bin`: Pages-served copy of the latest release bin.
- `docs/index.html`: site entry point.
- `scripts/sync_autoflash_firmware.py`: pulls latest release metadata and bin.
- `.github/workflows/sync_autoflash_firmware.yml`: scheduled/manual sync.

## Release Sync

Use:

```bash
python scripts/sync_autoflash_firmware.py --repo franssjz/cpr-vcodex
```

The sync should update the manifest, local firmware copy, site text, and README
release references together.

After sync, validate:

```bash
python scripts/pre_release_check.py --tag 1.2.0.39-cpr-vcodex --skip-build --allow-existing-tag
```

## GitHub Pages

Pages is intended to publish from `master` and `/docs`. Do not reintroduce a
`gh-pages` branch unless there is a strong reason and the release process is
updated accordingly.

Live URL:

```text
https://franssjz.github.io/cpr-vcodex/
```

If the public page shows an old version, check:

- repository Pages source settings;
- latest commit on `master`;
- `docs/firmware/manifest.json`;
- browser or CDN cache;
- the Pages deployment workflow.

## Flasher Safety

- Validate firmware size against the OTA app partition.
- Validate SHA-256 against the manifest.
- Refuse suspiciously small firmware.
- Keep Web Serial errors user-readable.
- Do not weaken partition-table validation without hardware testing.
