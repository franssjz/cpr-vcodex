from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_REPO = "franssjz/cpr-vcodex"
APP_PARTITION_SIZE = 6_553_600
MIN_FIRMWARE_SIZE = 1_000_000
VERSION_RE = re.compile(r"\b\d+\.\d+\.\d+\.\d+(?:[.-][0-9A-Za-z]+)?-[0-9A-Za-z._-]*cpr-vcodex\b")
DOWNLOAD_URL_RE = re.compile(
    r"https://github\.com/[^/]+/[^/]+/releases/download/[^/]+/[^\"'\s<>]+\.bin"
)


def request_json(url: str, token: str | None) -> dict[str, Any]:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "cpr-vcodex-autoflash-sync",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def download_bytes(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "cpr-vcodex-autoflash-sync"})
    with urllib.request.urlopen(request, timeout=300) as response:
        return response.read()


def select_firmware_asset(release: dict[str, Any]) -> dict[str, Any]:
    tag = str(release["tag_name"])
    assets = release.get("assets") or []
    if not assets:
        raise RuntimeError(f"Release {tag} has no downloadable assets")

    exact_name = f"{tag}.bin"
    for asset in assets:
        if asset.get("name") == exact_name:
            return asset

    for asset in assets:
        if asset.get("name") == "firmware.bin":
            return asset

    bin_assets = [asset for asset in assets if str(asset.get("name", "")).endswith(".bin")]
    if len(bin_assets) == 1:
        return bin_assets[0]

    names = ", ".join(str(asset.get("name", "<unnamed>")) for asset in assets)
    raise RuntimeError(f"Could not choose firmware asset from release {tag}. Assets: {names}")


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(delete=False, dir=path.parent) as tmp:
        tmp.write(data)
        tmp_path = Path(tmp.name)
    tmp_path.replace(path)


def update_text_file(path: Path, tag: str, download_url: str) -> bool:
    if not path.exists():
        return False

    original = path.read_text(encoding="utf-8")
    updated = VERSION_RE.sub(tag, original)
    updated = DOWNLOAD_URL_RE.sub(download_url, updated)
    if updated == original:
        return False

    path.write_text(updated, encoding="utf-8", newline="")
    return True


def sync_autoflash(repo: str, project_dir: Path, token: str | None) -> str:
    release = request_json(f"https://api.github.com/repos/{repo}/releases/latest", token)
    if release.get("draft") or release.get("prerelease"):
        raise RuntimeError(f"Latest release is not a stable published release: {release.get('tag_name')}")

    tag = str(release["tag_name"])
    asset = select_firmware_asset(release)
    download_url = str(asset["browser_download_url"])
    firmware = download_bytes(download_url)
    firmware_size = len(firmware)
    expected_size = asset.get("size")

    if expected_size is not None and int(expected_size) != firmware_size:
        raise RuntimeError(f"Downloaded firmware size mismatch: asset={expected_size}, downloaded={firmware_size}")
    if firmware_size < MIN_FIRMWARE_SIZE:
        raise RuntimeError(f"Downloaded firmware is suspiciously small: {firmware_size} bytes")
    if firmware_size > APP_PARTITION_SIZE:
        raise RuntimeError(f"Downloaded firmware is too large for the app partition: {firmware_size} bytes")

    sha256 = hashlib.sha256(firmware).hexdigest()
    firmware_dir = project_dir / "docs" / "firmware"
    write_atomic(firmware_dir / "firmware.bin", firmware)

    manifest = {
        "name": "CPR-vCodex",
        "version": tag,
        "firmwareUrl": "firmware/firmware.bin",
        "downloadUrl": download_url,
        "size": firmware_size,
        "sha256": sha256,
        "source": {
            "type": "github-release",
            "repo": repo,
            "tag": tag,
            "asset": asset.get("name"),
        },
        "new_install_prompt_erase": False,
        "builds": [
            {
                "chipFamily": "ESP32-C3",
                "parts": [
                    {
                        "path": "firmware.bin",
                        "offset": 65536,
                    }
                ],
            }
        ],
    }
    (firmware_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

    for relative in ("docs/assets/site.js", "docs/index.html", "docs/flash.html"):
        update_text_file(project_dir / relative, tag, download_url)

    env_path = os.environ.get("GITHUB_ENV")
    if env_path:
        with open(env_path, "a", encoding="utf-8") as env_file:
            env_file.write(f"AUTOFLASH_VERSION={tag}\n")

    print(f"Synced auto-flash firmware to {tag}")
    print(f"Asset: {asset.get('name')}")
    print(f"Size: {firmware_size}")
    print(f"SHA-256: {sha256}")
    return tag


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync GitHub Pages auto-flash firmware from latest stable release.")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPO))
    parser.add_argument("--project-dir", type=Path, default=Path.cwd())
    args = parser.parse_args()

    try:
        sync_autoflash(args.repo, args.project_dir.resolve(), os.environ.get("GITHUB_TOKEN"))
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
