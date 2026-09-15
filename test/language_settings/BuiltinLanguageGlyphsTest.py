"""Every language must remain identifiable in the built-in language picker."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_i18n import parse_yaml_file


class BuiltinLanguageGlyphsTest(unittest.TestCase):
    def test_ui_faces_cover_all_language_names(self):
        names = [parse_yaml_file(str(path))["_language_name"]
                 for path in (ROOT / "lib/I18n/translations").glob("*.yaml")]
        self.assertGreaterEqual(len(names), 24)
        for font in ("ubuntu_10_regular", "ubuntu_10_bold", "ubuntu_12_regular",
                     "ubuntu_12_bold", "notosans_8_regular"):
            source = (ROOT / "lib/EpdFont/builtinFonts" / (font + ".h")).read_text(encoding="utf-8")
            table = source.split("Intervals[] = {", 1)[1].split("};", 1)[0]
            intervals = [(int(first, 16), int(last, 16)) for first, last in
                         re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),", table)]
            self.assertTrue(intervals, font)
            for name in names:
                with self.subTest(font=font, language=name):
                    missing = {"U+%04X" % ord(char) for char in name
                               if not any(first <= ord(char) <= last for first, last in intervals)}
                    self.assertFalse(missing, "%s cannot render %s: %s" % (font, name, missing))


if __name__ == "__main__":
    unittest.main()
