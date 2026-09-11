import json
import re
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FLASH_HTML = PROJECT_ROOT / "docs" / "flash.html"
MANIFEST = PROJECT_ROOT / "docs" / "firmware" / "manifest.json"


class FlashPageRegressionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.html = FLASH_HTML.read_text(encoding="utf-8")

    def test_x4_is_the_default_device_in_markup_and_script(self):
        self.assertRegex(
            self.html,
            re.compile(r'<button(?=[^>]*data-device="x4")(?=[^>]*aria-checked="true")[^>]*>'),
        )
        self.assertRegex(
            self.html,
            re.compile(
                r'<button(?=[^>]*data-device="x4pro")(?=[^>]*aria-checked="false")'
                r'(?=[^>]*aria-disabled="true")(?=[^>]*disabled)[^>]*>'
            ),
        )
        self.assertIn('let selectedDevice = "x4";', self.html)

    def test_device_choice_is_not_restored_between_visits(self):
        self.assertNotIn("cprVcodexFlashDevice", self.html)
        self.assertNotRegex(self.html, r"localStorage\.(?:getItem|setItem)\([^)]*Device")

    def test_x4pro_is_blocked_even_with_stale_metadata(self):
        self.assertRegex(
            self.html,
            re.compile(r"x4pro:\s*\{[^}]*distributionBlocked:\s*true", re.DOTALL),
        )
        self.assertIn("if (device.distributionBlocked) return null;", self.html)
        self.assertNotIn("firmware-x4pro.bin", self.html)

    def test_manifest_and_release_workflows_publish_c3_only(self):
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(set(manifest["devices"]), {"x4", "x3"})
        self.assertFalse((MANIFEST.parent / "firmware-x4pro.bin").exists())

        release_workflow = (PROJECT_ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        rc_workflow = (PROJECT_ROOT / ".github" / "workflows" / "release_candidate.yml").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("x4pro-gh_release", release_workflow)
        self.assertNotIn("x4pro-gh_release_rc", rc_workflow)


if __name__ == "__main__":
    unittest.main()
