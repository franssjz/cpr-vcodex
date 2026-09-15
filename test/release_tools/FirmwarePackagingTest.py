import json
import os
from pathlib import Path
import runpy
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def image_header(version="test", chip=5):
    header = bytearray(288)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into("<H", header, 12, chip)
    struct.pack_into("<I", header, 28, 256)
    struct.pack_into("<I", header, 32, 0xABCD5432)
    encoded = version.encode("ascii")
    header[48:48 + len(encoded)] = encoded
    return header


class Environment:
    def __init__(self, root=None, profile="default"):
        self.root = root
        self.profile = profile

    def Alias(self, *_):
        return "package_vcodex"

    def AlwaysBuild(self, *_):
        pass

    def Default(self, *_):
        pass

    def subst(self, value):
        return {
            "$BUILD_DIR": str(self.root / "build"),
            "$PROJECT_DIR": str(self.root),
            "$PROGNAME": "firmware",
            "$PIOENV": self.profile,
        }[value]


SCRIPT = runpy.run_path(
    str(ROOT / "scripts/package_vcodex_bin.py"),
    init_globals={"Import": lambda *_: None, "env": Environment()},
)


class FirmwarePackagingTest(unittest.TestCase):
    def test_release_build_preserves_published_readme_version(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {"VCODEX_RELEASE_DRY_RUN": "0"}):
            root = Path(directory)
            (root / "build").mkdir()
            (root / "artifacts").mkdir()
            readme = "| Current release (CPR-vCodex) build | [`1.6.0.33-cpr-vcodex`](https://example.com/33) |\n"
            (root / "README.md").write_text(readme)
            (root / "artifacts/build-version.json").write_text(
                json.dumps({"version": "1.6.0.36", "baseVersion": "1.6.0", "buildSeq": 36})
            )
            (root / "build/firmware.bin").write_bytes(image_header("1.6.0.36"))
            SCRIPT["package_vcodex_bin"](None, None, Environment(root, "gh_release"))
            self.assertTrue((root / "artifacts/1.6.0.36-cpr-vcodex.bin").exists())
            self.assertEqual((root / "README.md").read_text(), readme)
            self.assertEqual((root / "artifacts/.release-counter-1-6-0.txt").read_text().strip(), "36")

    def test_exact_x4_slot_size_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            (root / "artifacts").mkdir()
            (root / "artifacts/build-version.json").write_text(json.dumps({"version": "test"}))
            with (root / "build/firmware.bin").open("wb") as file:
                file.write(image_header())
                file.truncate(0x640000)
            SCRIPT["package_vcodex_bin"](None, None, Environment(root))
            self.assertEqual((root / "artifacts/test-cpr-vcodex.bin").stat().st_size, 0x640000)

    def test_final_bin_overflow_is_rejected_before_artifacts_or_metadata(self):
        for profile, limit in (("default", 0x640000), ("gh_release", 0x640000), ("x4pro", 0x7E0000)):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "build").mkdir()
                with (root / "build/firmware.bin").open("wb") as file:
                    file.truncate(limit + 1)
                with self.assertRaisesRegex(ValueError, "Refusing to package"):
                    SCRIPT["package_vcodex_bin"](None, None, Environment(root, profile))
                self.assertFalse((root / "artifacts").exists())

    def test_stale_version_and_wrong_board_are_rejected_before_packaging(self):
        for header in (image_header("old"), image_header(chip=9), b"invalid",
                       image_header("x" * 32)):
            with self.subTest(header=header[:2]), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "build").mkdir()
                (root / "artifacts").mkdir()
                (root / "artifacts/build-version.json").write_text(json.dumps({"version": "test"}))
                (root / "build/firmware.bin").write_bytes(header)
                with self.assertRaises(ValueError):
                    SCRIPT["package_vcodex_bin"](None, None, Environment(root))
                self.assertFalse((root / "artifacts/test-cpr-vcodex.bin").exists())


if __name__ == "__main__":
    unittest.main()
