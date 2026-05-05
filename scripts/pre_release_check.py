from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path


APP_PARTITION_SIZE = 6_553_600
RAM_RE = re.compile(r"RAM:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")
FLASH_RE = re.compile(r"Flash:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")


def run(cmd: list[str], *, env: dict[str, str] | None = None, check: bool = False) -> subprocess.CompletedProcess[str]:
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        cmd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=merged_env,
        check=check,
    )


def ok(message: str) -> None:
    print(f"[ok] {message}")


def fail(message: str) -> None:
    raise RuntimeError(message)


def get_base_version(project_dir: Path) -> str:
    config = configparser.ConfigParser()
    config.read(project_dir / "platformio.ini", encoding="utf-8")
    return config.get("crosspoint", "version")


def parse_release_tag(tag: str, base_version: str) -> int:
    match = re.fullmatch(rf"{re.escape(base_version)}\.(\d+)-cpr-vcodex", tag)
    if not match:
        fail(f"Tag must match {base_version}.<release>-cpr-vcodex, got {tag!r}")
    return int(match.group(1))


def require_clean_worktree(allow_dirty: bool) -> None:
    status = run(["git", "status", "--short"], check=True).stdout.strip()
    if status and not allow_dirty:
        fail("Working tree is not clean. Commit or stash changes, or pass --allow-dirty.")
    if status:
        print("[warn] Working tree is dirty; continuing because --allow-dirty was set.")
    else:
        ok("working tree is clean")


def require_tag_available(tag: str, allow_existing_tag: bool) -> None:
    local = run(["git", "rev-parse", "--verify", f"refs/tags/{tag}"])
    remote = run(["git", "ls-remote", "--exit-code", "--tags", "origin", f"refs/tags/{tag}"])
    exists = local.returncode == 0 or remote.returncode == 0
    if exists and not allow_existing_tag:
        fail(f"Tag {tag} already exists. Pass --allow-existing-tag to validate an existing tag.")
    if exists:
        print(f"[warn] Tag {tag} already exists; continuing because --allow-existing-tag was set.")
    else:
        ok(f"tag {tag} is available")


def parse_size(regex: re.Pattern[str], output: str, label: str) -> tuple[int, int]:
    match = regex.search(output)
    if not match:
        fail(f"Could not parse {label} usage from PlatformIO output")
    return int(match.group(1)), int(match.group(2))


def build_release(project_dir: Path, tag: str, jobs: int) -> str:
    env = {
        "PYTHONIOENCODING": "utf-8",
        "PYTHONUTF8": "1",
        "VCODEX_RELEASE_DRY_RUN": "1",
        "VCODEX_RELEASE_TAG": tag,
    }
    cmd = ["python", "-X", "utf8", "-m", "platformio", "run", "-e", "gh_release", "-j", str(jobs)]
    print(f"[run] {' '.join(cmd)}")
    result = run(cmd, env=env)
    if result.returncode != 0:
        print(result.stdout)
        fail(f"Release build failed with exit code {result.returncode}")
    ok("gh_release dry-run build succeeded")
    return result.stdout


def validate_budget(output: str, flash_budget_percent: float) -> None:
    flash_used, flash_total = parse_size(FLASH_RE, output, "flash")
    ram_used, ram_total = parse_size(RAM_RE, output, "RAM")
    budget_bytes = int(APP_PARTITION_SIZE * flash_budget_percent / 100)

    if flash_total != APP_PARTITION_SIZE:
        print(f"[warn] PlatformIO app partition size changed: {flash_total} bytes")
    if flash_used > budget_bytes:
        fail(f"Flash usage {flash_used} bytes exceeds {flash_budget_percent:.1f}% budget ({budget_bytes} bytes)")

    ok(f"flash budget: {flash_used}/{APP_PARTITION_SIZE} bytes ({flash_used / APP_PARTITION_SIZE * 100:.1f}%)")
    ok(f"RAM usage: {ram_used}/{ram_total} bytes ({ram_used / ram_total * 100:.1f}%)")


def validate_artifacts(project_dir: Path, tag: str, release_seq: int, base_version: str) -> None:
    artifact_path = project_dir / "artifacts" / f"{tag}.bin"
    metadata_path = project_dir / "artifacts" / f"{tag}.json"
    if not artifact_path.exists():
        fail(f"Missing packaged firmware artifact: {artifact_path}")
    if not metadata_path.exists():
        fail(f"Missing packaged metadata artifact: {metadata_path}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    expected_version = f"{base_version}.{release_seq}"
    checks = {
        "artifactName": f"{tag}.bin",
        "environment": "gh_release",
        "version": expected_version,
        "buildSequence": release_seq,
    }
    for key, expected in checks.items():
        if metadata.get(key) != expected:
            fail(f"Metadata {key} mismatch: expected {expected!r}, got {metadata.get(key)!r}")

    firmware_bytes = artifact_path.stat().st_size
    if metadata.get("firmwareBytes") != firmware_bytes:
        fail(f"Metadata firmwareBytes mismatch: expected {firmware_bytes}, got {metadata.get('firmwareBytes')}")
    if firmware_bytes > APP_PARTITION_SIZE:
        fail(f"Packaged firmware is too large for the app partition: {firmware_bytes} bytes")

    ok(f"release artifacts match tag {tag} ({firmware_bytes} bytes)")


def validate_autoflash_manifest(project_dir: Path) -> None:
    manifest_path = project_dir / "docs" / "firmware" / "manifest.json"
    firmware_path = project_dir / "docs" / "firmware" / "firmware.bin"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    firmware = firmware_path.read_bytes()
    digest = hashlib.sha256(firmware).hexdigest()

    if manifest.get("firmwareUrl") != "firmware/firmware.bin":
        fail("Auto-flash manifest must use local firmware/firmware.bin")
    if manifest.get("size") != len(firmware):
        fail("Auto-flash manifest size does not match docs/firmware/firmware.bin")
    if manifest.get("sha256") != digest:
        fail("Auto-flash manifest sha256 does not match docs/firmware/firmware.bin")
    if (manifest.get("source") or {}).get("type") != "github-release":
        fail("Auto-flash manifest source.type must be github-release")

    ok(f"auto-flash manifest matches published firmware copy ({manifest.get('version')})")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate CPR-vCodex release readiness before pushing a stable tag.")
    parser.add_argument("--tag", required=True, help="Candidate stable tag, e.g. 1.2.0.39-cpr-vcodex")
    parser.add_argument("--jobs", type=int, default=1, help="PlatformIO build jobs (default: 1)")
    parser.add_argument("--flash-budget-percent", type=float, default=97.5)
    parser.add_argument("--skip-build", action="store_true", help="Validate existing artifacts without rebuilding")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--allow-existing-tag", action="store_true")
    args = parser.parse_args()

    project_dir = Path.cwd()
    try:
        base_version = get_base_version(project_dir)
        release_seq = parse_release_tag(args.tag, base_version)
        require_clean_worktree(args.allow_dirty)
        require_tag_available(args.tag, args.allow_existing_tag)

        if args.skip_build:
            print("[warn] Skipping gh_release build; validating existing artifacts only.")
        else:
            output = build_release(project_dir, args.tag, args.jobs)
            validate_budget(output, args.flash_budget_percent)

        validate_artifacts(project_dir, args.tag, release_seq, base_version)
        validate_autoflash_manifest(project_dir)
        ok(f"pre-release checks passed for {args.tag}")
        return 0
    except RuntimeError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
