from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

Import("env")

README_PATH = "README.md"
RELEASE_COUNTER_FILE_TEMPLATE = ".release-counter-{base}.txt"
RELEASE_DRY_RUN_ENV = "VCODEX_RELEASE_DRY_RUN"


def extract_define(name: str) -> str:
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, tuple) and len(define) >= 2 and define[0] == name:
            value = define[1]
            if isinstance(value, str):
                return value.replace('\\"', '"').strip('"')
            return str(value)
    return "unknown"


def sanitize_filename(value: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z._+-]+", "-", value).strip(".-")
    return sanitized or "unknown"


def extract_define_int(name: str) -> int | None:
    value = extract_define(name)
    try:
        return int(value)
    except ValueError:
        return None


def counter_token(base_version: str) -> str:
    return re.sub(r"[^0-9A-Za-z]+", "-", base_version).strip("-") or "unknown"


def extract_base_version(version: str) -> str:
    explicit_base = extract_define("VCODEX_BASE_VERSION")
    if explicit_base != "unknown":
        return explicit_base

    match = re.fullmatch(r"(\d+\.\d+\.\d+)\.\d+(?:\..*)?", version)
    if match:
        return match.group(1)
    return "unknown"


def release_counter_path(project_dir: Path, base_version: str) -> Path:
    return project_dir / "artifacts" / RELEASE_COUNTER_FILE_TEMPLATE.format(base=counter_token(base_version))


def update_readme_release_version(project_dir: Path, artifact_name: str) -> None:
    readme_path = project_dir / README_PATH
    if not readme_path.exists():
        print(f"README release update skipped: missing {readme_path}")
        return

    readme_text = readme_path.read_text(encoding="utf-8")
    release_name = artifact_name[:-4]
    release_url = f"https://github.com/franssjz/cpr-vcodex/releases/tag/{release_name}"
    updated_text, replacements = re.subn(
        r"(\| Current release \(CPR-vCodex\) build \| \[`)([^\]]+)(`\]\()([^)]+)(\) \|)",
        rf"\g<1>{release_name}\g<3>{release_url}\g<5>",
        readme_text,
        count=1,
    )

    if replacements == 0:
        print(f"README release update skipped: marker not found in {readme_path}")
        return

    if updated_text != readme_text:
        readme_path.write_text(updated_text, encoding="utf-8", newline="")
        print(f"Updated README current release to {artifact_name[:-4]}")


def persist_release_counter(project_dir: Path, base_version: str, build_seq: int | None) -> None:
    if build_seq is None:
        print("Release counter update skipped: missing VCODEX_BUILD_SEQ")
        return

    counter_path = release_counter_path(project_dir, base_version)
    counter_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = counter_path.with_suffix(counter_path.suffix + ".tmp")
    temp_path.write_text(f"{build_seq}\n", encoding="utf-8")
    temp_path.replace(counter_path)
    print(f"Advanced release counter to {build_seq} ({counter_path})")


def package_vcodex_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    progname = env.subst("$PROGNAME")
    project_dir = Path(env.subst("$PROJECT_DIR"))

    firmware_path = build_dir / f"{progname}.bin"
    if not firmware_path.exists():
        print(f"vcodex packaging skipped: missing {firmware_path}")
        return

    version = extract_define("CROSSPOINT_VERSION")
    base_version = extract_base_version(version)
    build_seq = extract_define_int("VCODEX_BUILD_SEQ")
    safe_version = sanitize_filename(version)

    output_dir = project_dir / "artifacts"
    output_dir.mkdir(parents=True, exist_ok=True)

    artifact_name = f"{safe_version}-cpr-vcodex.bin"
    artifact_path = output_dir / artifact_name
    shutil.copy2(firmware_path, artifact_path)

    metadata = {
        "version": version,
        "safeVersion": safe_version,
        "artifactName": artifact_name,
        "artifactPath": str(artifact_path),
        "firmwareBytes": artifact_path.stat().st_size,
        "sourceBin": str(firmware_path),
        "environment": env.subst("$PIOENV"),
    }
    if build_seq is not None:
        metadata["buildSequence"] = build_seq
    metadata_path = output_dir / f"{safe_version}-cpr-vcodex.json"
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    if env.subst("$PIOENV") == "gh_release" and os.environ.get(RELEASE_DRY_RUN_ENV) == "1":
        print(f"Release metadata update skipped: {RELEASE_DRY_RUN_ENV}=1")
    elif env.subst("$PIOENV") == "gh_release":
        persist_release_counter(project_dir, base_version, build_seq)
        update_readme_release_version(project_dir, artifact_name)

    print(f"Packaged vcodex artifact: {artifact_path}")
    print(f"Wrote vcodex metadata: {metadata_path}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package_vcodex_bin)
