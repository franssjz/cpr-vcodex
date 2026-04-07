"""
PlatformIO post-build helper:
- prints the final firmware.bin size against the OTA app partition limit
- creates a versioned copy for vCodex release builds
"""

from __future__ import annotations

import configparser
import csv
import os
import shutil


def load_versions(project_dir: str) -> tuple[str | None, str | None]:
    ini_path = os.path.join(project_dir, "platformio.ini")
    config = configparser.ConfigParser()
    config.read(ini_path, encoding="utf-8")
    vcodex_version = config.get("vcodex", "version", fallback=None)
    crosspoint_version = config.get("crosspoint", "version", fallback=None)
    return vcodex_version, crosspoint_version


def load_app_partition_size(project_dir: str) -> int | None:
    partitions_path = os.path.join(project_dir, "partitions.csv")
    if not os.path.isfile(partitions_path):
        return None

    with open(partitions_path, "r", encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            if row[0].strip() == "app0":
                raw_size = row[4].strip()
                return int(raw_size, 0)
    return None


def after_build(target, source, env):
    bin_path = str(target[0])
    if not os.path.isfile(bin_path):
        print(f"Post-build: firmware bin not found at {bin_path}")
        return

    project_dir = env["PROJECT_DIR"]
    pio_env = env["PIOENV"]
    bin_size = os.path.getsize(bin_path)
    app_limit = load_app_partition_size(project_dir)

    if app_limit:
        margin = app_limit - bin_size
        usage = (bin_size / app_limit) * 100
        print(
            f"Post-build: firmware.bin = {bin_size} bytes | "
            f"app partition = {app_limit} bytes | "
            f"usage = {usage:.1f}% | margin = {margin} bytes"
        )

    if pio_env != "vcodex_release":
        return

    vcodex_version, _ = load_versions(project_dir)
    if not vcodex_version:
        print("Post-build: vCodex version not found, skipping versioned copy")
        return

    output_dir = os.path.dirname(bin_path)
    versioned_name = f"vcodex-{vcodex_version}.bin"
    versioned_path = os.path.join(output_dir, versioned_name)
    shutil.copy2(bin_path, versioned_path)
    print(f"Post-build: created {versioned_name}")


Import("env")
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)
