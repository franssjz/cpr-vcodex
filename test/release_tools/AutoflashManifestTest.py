import unittest

from scripts.sync_autoflash_firmware import C3_TARGET, build_manifest, pages_firmware_url


class AutoflashManifestTest(unittest.TestCase):
    def test_installed_ota_uses_redirect_free_pages_copy(self):
        tag = "1.6.0.33-cpr-vcodex"
        release_url = f"https://github.com/franssjz/cpr-vcodex/releases/download/{tag}/{tag}.bin"
        release = {
            "tag_name": tag,
            "published_at": "2026-09-15T00:00:00Z",
        }
        image = {
            "asset": {"name": f"{tag}.bin", "updated_at": "2026-09-15T00:00:00Z"},
            "downloadUrl": release_url,
            "size": 6_391_952,
            "sha256": "a" * 64,
        }

        manifest = build_manifest("franssjz/cpr-vcodex", release, {C3_TARGET.key: image})

        self.assertEqual(
            manifest["downloadUrl"],
            "https://franssjz.github.io/cpr-vcodex/firmware/firmware.bin",
        )
        self.assertEqual(manifest["devices"]["x4"]["downloadUrl"], release_url)
        self.assertEqual(manifest["devices"]["x3"]["downloadUrl"], release_url)

    def test_pages_url_rejects_invalid_repository_name(self):
        with self.assertRaises(ValueError):
            pages_firmware_url("cpr-vcodex")


if __name__ == "__main__":
    unittest.main()
