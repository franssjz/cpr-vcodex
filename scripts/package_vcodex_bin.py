from __future__ import annotations

import json
import os
import re
import shutil
import struct
from pathlib import Path

Import("env")

BUILD_VERSION_JSON_PATH = "artifacts/build-version.json"
RELEASE_COUNTER_FILE_TEMPLATE = ".release-counter-{base}.txt"
RELEASE_DRY_RUN_ENV = "VCODEX_RELEASE_DRY_RUN"


def load_build_metadata(project_dir: Path) -> tuple[str, str, int | None]:
    path = project_dir / BUILD_VERSION_JSON_PATH
    if not path.exists():
        return "unknown", "unknown", None

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "unknown", "unknown", None

    version = str(data.get("version", "unknown"))
    base_version = str(data.get("baseVersion", infer_base_version(version)))
    build_seq_value = data.get("buildSeq")
    build_seq = int(build_seq_value) if isinstance(build_seq_value, int) else None
    return version, base_version, build_seq


def sanitize_filename(value: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z._+-]+", "-", value).strip(".-")
    return sanitized or "unknown"


def counter_token(base_version: str) -> str:
    return re.sub(r"[^0-9A-Za-z]+", "-", base_version).strip("-") or "unknown"


def infer_base_version(version: str) -> str:
    match = re.fullmatch(r"(\d+\.\d+\.\d+)\.\d+(?:\..*)?", version)
    if match:
        return match.group(1)
    match = re.fullmatch(r"(\d+\.\d+\.\d+)(?:-.*)?", version)
    if match:
        return match.group(1)
    return "unknown"


def release_counter_path(project_dir: Path, base_version: str) -> Path:
    return project_dir / "artifacts" / RELEASE_COUNTER_FILE_TEMPLATE.format(base=counter_token(base_version))


def persist_release_counter(project_dir: Path, base_version: str, build_seq: int | None) -> None:
    if build_seq is None:
        print("Release counter update skipped: missing buildSeq in build-version.json")
        return

    counter_path = release_counter_path(project_dir, base_version)
    counter_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = counter_path.with_suffix(counter_path.suffix + ".tmp")
    temp_path.write_text(f"{build_seq}\n", encoding="utf-8")
    temp_path.replace(counter_path)
    print(f"Advanced release counter to {build_seq} ({counter_path})")


def validate_application_description(firmware_path: Path, version: str, pio_env: str) -> None:
    # ESP image header (24 bytes), first segment header (8), esp_app_desc (256).
    # The descriptor must begin the first DROM segment, as required by ESP-IDF.
    with firmware_path.open("rb") as firmware:
        header = firmware.read(288)
    expected_chip = 9 if pio_env.startswith("x4pro") else 5
    if (len(header) != 288 or header[0] != 0xE9 or header[1] == 0
            or struct.unpack_from("<H", header, 12)[0] != expected_chip
            or struct.unpack_from("<I", header, 28)[0] < 256
            or struct.unpack_from("<I", header, 32)[0] != 0xABCD5432):
        raise ValueError(f"Invalid ESP application description or chip for {pio_env}")
    raw_version = header[48:80]
    if b"\0" not in raw_version:
        raise ValueError("Application version is not null-terminated")
    embedded_version = raw_version.split(b"\0", 1)[0].decode("ascii")
    if version == "unknown" or embedded_version != version:
        raise ValueError(f"Application version {embedded_version!r} differs from build version {version!r}")


def package_vcodex_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    progname = env.subst("$PROGNAME")
    project_dir = Path(env.subst("$PROJECT_DIR"))

    firmware_path = build_dir / f"{progname}.bin"
    if not firmware_path.exists():
        print(f"vcodex packaging skipped: missing {firmware_path}")
        return

    pio_env = env.subst("$PIOENV")
    slot_size = 0x7E0000 if pio_env.startswith("x4pro") else 0x640000
    firmware_size = firmware_path.stat().st_size
    if firmware_size > slot_size:
        raise ValueError(
            f"Refusing to package {pio_env}: final BIN is {firmware_size:,} bytes, "
            f"but its OTA slot is {slot_size:,} bytes. The ELF size does not include all image padding."
        )

    version, base_version, build_seq = load_build_metadata(project_dir)
    validate_application_description(firmware_path, version, pio_env)
    safe_version = sanitize_filename(version)

    output_dir = project_dir / "artifacts"
    output_dir.mkdir(parents=True, exist_ok=True)

    # Board suffix for internal ESP32-S3 X4 Pro builds. Public distribution is
    # withdrawn; keeping distinct local names prevents C3/X4 Pro confusion in
    # maintainer diagnostics. Dev versions already carry the suffix.
    board_suffix = ""
    if pio_env.startswith("x4pro") and not safe_version.endswith("-x4pro"):
        board_suffix = "-x4pro"
    artifact_stem = f"{safe_version}-cpr-vcodex{board_suffix}"

    artifact_name = f"{artifact_stem}.bin"
    artifact_path = output_dir / artifact_name
    shutil.copy2(firmware_path, artifact_path)

    metadata = {
        "version": version,
        "safeVersion": safe_version,
        "artifactName": artifact_name,
        "artifactPath": str(artifact_path),
        "firmwareBytes": artifact_path.stat().st_size,
        "sourceBin": str(firmware_path),
        "environment": pio_env,
        "board": "x4pro" if pio_env.startswith("x4pro") else "x4",
        "applicationVersion": version,
    }
    if build_seq is not None:
        metadata["buildSequence"] = build_seq
    metadata_path = output_dir / f"{artifact_stem}.json"
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    # Only the C3 release env owns the release counter. Published-version text
    # is updated by sync_autoflash_firmware after fetching a published release,
    # never by building a local candidate.
    if pio_env == "gh_release" and os.environ.get(RELEASE_DRY_RUN_ENV) == "1":
        print(f"Release metadata update skipped: {RELEASE_DRY_RUN_ENV}=1")
    elif pio_env == "gh_release":
        persist_release_counter(project_dir, base_version, build_seq)

    print(f"Packaged vcodex artifact: {artifact_path}")
    print(f"Wrote vcodex metadata: {metadata_path}")


# A post-action on firmware.bin is skipped when SCons restores that node from
# cache. Packaging must still validate and copy the current image on every
# default build, including when the artifacts directory has been removed.
package_target = env.Alias("package_vcodex", "$BUILD_DIR/${PROGNAME}.bin", package_vcodex_bin)
env.AlwaysBuild(package_target)
env.Default(package_target)
