from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from urllib.parse import quote
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_REPO = "franssjz/cpr-vcodex"
APP_PARTITION_SIZE = 6_553_600
MIN_FIRMWARE_SIZE = 1_000_000
VERSION_RE = re.compile(r"\b\d+\.\d+\.\d+\.\d+(?:[.-][0-9A-Za-z]+)?-[0-9A-Za-z._-]*cpr-vcodex\b")
FIRMWARE_TAG_RE = re.compile(r"^\d+\.\d+\.\d+\.\d+(?:[.-][0-9A-Za-z]+)?-cpr-vcodex$")
DOWNLOAD_URL_RE = re.compile(
    r"https://github\.com/[^/]+/[^/]+/releases/download/[^/]+/[^\"'\s<>]+\.bin"
)
FIRMWARE_VERSION_PREFIX_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)")
FALLBACK_RELEASE_RE = re.compile(r"(const FALLBACK_RELEASE = \{.*?\n    \};)", re.DOTALL)


def request_json(url: str, token: str | None) -> Any:
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


def firmware_tag_sort_key(tag: str) -> tuple[int, int, int, int, int, str] | None:
    if not FIRMWARE_TAG_RE.fullmatch(tag):
        return None

    prefix = tag.removesuffix("-cpr-vcodex")
    match = FIRMWARE_VERSION_PREFIX_RE.match(prefix)
    if not match:
        return None

    numbers = tuple(int(part) for part in match.groups())
    stable = 1 if prefix == ".".join(str(number) for number in numbers) else 0
    return (*numbers, stable, tag)


def fetch_firmware_release_by_tag(repo: str, tag: str, token: str | None) -> dict[str, Any]:
    release = request_json(
        f"https://api.github.com/repos/{repo}/releases/tags/{quote(tag, safe='')}",
        token,
    )
    if not isinstance(release, dict):
        raise RuntimeError(f"GitHub release response for {tag} was not an object")
    if release.get("draft") or release.get("prerelease"):
        raise RuntimeError(f"Release {tag} is not a stable published firmware release")
    if not FIRMWARE_TAG_RE.fullmatch(str(release.get("tag_name", ""))):
        raise RuntimeError(f"Release {tag} is not a CPR-vCodex firmware release")

    select_firmware_asset(release)
    return release


def fetch_latest_firmware_release(repo: str, token: str | None) -> dict[str, Any]:
    releases = request_json(f"https://api.github.com/repos/{repo}/releases?per_page=50", token)
    if not isinstance(releases, list):
        raise RuntimeError("GitHub releases response was not a list")

    candidates: list[tuple[tuple[int, int, int, int, int, str], dict[str, Any]]] = []
    for release in releases:
        if release.get("draft") or release.get("prerelease"):
            continue

        tag = str(release.get("tag_name", ""))
        sort_key = firmware_tag_sort_key(tag)
        if sort_key is None:
            continue

        select_firmware_asset(release)
        candidates.append((sort_key, release))

    if candidates:
        return max(candidates, key=lambda item: item[0])[1]

    raise RuntimeError("Could not find a stable CPR-vCodex firmware release")


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


def update_flash_fallback(path: Path, tag: str, download_url: str, firmware_size: int, sha256: str) -> bool:
    if not path.exists():
        return False

    original = path.read_text(encoding="utf-8")
    match = FALLBACK_RELEASE_RE.search(original)
    if not match:
        return False

    block = match.group(1)
    updated_block = VERSION_RE.sub(tag, block)
    updated_block = DOWNLOAD_URL_RE.sub(download_url, updated_block)
    updated_block = re.sub(r'(size:\s*)\d+(,)', rf"\g<1>{firmware_size}\2", updated_block, count=1)
    updated_block = re.sub(r'(sha256:\s*")[0-9a-fA-F]+(")', rf"\g<1>{sha256}\2", updated_block, count=1)

    if updated_block == block:
        return False

    updated = original[: match.start(1)] + updated_block + original[match.end(1) :]
    path.write_text(updated, encoding="utf-8", newline="")
    return True


def sync_autoflash(repo: str, project_dir: Path, token: str | None, tag: str | None = None) -> str:
    release = fetch_firmware_release_by_tag(repo, tag, token) if tag else fetch_latest_firmware_release(repo, token)
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
            "publishedAt": release.get("published_at"),
            "assetUpdatedAt": asset.get("updated_at"),
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

    for relative in ("README.md", "docs/assets/site.js", "docs/index.html", "docs/flash.html"):
        update_text_file(project_dir / relative, tag, download_url)
    update_flash_fallback(project_dir / "docs" / "flash.html", tag, download_url, firmware_size, sha256)

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
    parser.add_argument("--tag", default=os.environ.get("AUTOFLASH_TAG"))
    args = parser.parse_args()

    try:
        sync_autoflash(args.repo, args.project_dir.resolve(), os.environ.get("GITHUB_TOKEN"), args.tag)
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
