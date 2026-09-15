"""Verify shipped kerning against the pre-compaction font data, cell for cell.

Golden hashes were captured from 08180f99's dense tables. The remainder hash
also locks glyphs, metrics, character coverage, class maps and ligatures.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "compact", ROOT / "lib/EpdFont/scripts/compact_builtin_kerning.py")
COMPACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPACT)


def snapshot(text):
    name = re.search(r"static const EpdFontData (\w+) =", text)[1]
    dense_match = re.search(rf"static const int8_t {name}KernMatrix\[\] = \{{(.*?)\n\}};", text, re.S)

    def numbers(suffix):
        block = re.search(rf"static const \w+ {name}{suffix}\[\] = \{{(.*?)\n\}};", text, re.S)
        return [int(v.strip()) for v in block[1].split(",") if v.strip()]

    descriptor = re.search(rf"static const EpdFontData {name} = \{{(.*?)\n\}};", text, re.S)[1]
    fields = [re.sub(r"//.*", "", line).strip().rstrip(",")
              for line in descriptor.splitlines() if re.sub(r"//.*", "", line).strip()]
    rows, cols = int(fields[23]), int(fields[24])
    if dense_match:
        dense = numbers("KernMatrix")
    else:
        offsets, columns, values = (numbers(s) for s in ("KernRowOffsets", "KernSparseCols", "KernSparseValues"))
        assert len(offsets) == rows + 1 and offsets[0] == 0
        assert offsets[-1] == len(columns) == len(values)
        dense = [0] * (rows * cols)
        for row in range(rows):
            assert offsets[row] <= offsets[row + 1]
            row_cols = columns[offsets[row]:offsets[row + 1]]
            assert row_cols == sorted(set(row_cols))
            for i in range(offsets[row], offsets[row + 1]):
                assert 0 <= columns[i] < cols and values[i] != 0
                dense[row * cols + columns[i]] = values[i]
        assert fields[17:21] == ["nullptr", name + "KernRowOffsets", name + "KernSparseCols", name + "KernSparseValues"]
    assert len(dense) == rows * cols
    remainder = re.sub(rf"static const \w+ {name}Kern(?:Matrix|RowOffsets|SparseCols|SparseValues)\[\] = \{{.*?\n\}};\s*",
                       "", text, flags=re.S)
    # Ignore only the four descriptor pointers whose representation changed.
    remainder = re.sub(r"    (?:" + name + r"KernMatrix|nullptr),[^\n]*\n"
                       r"    (?:nullptr|" + name + r"KernRowOffsets),[^\n]*\n"
                       r"    (?:nullptr|" + name + r"KernSparseCols),[^\n]*\n"
                       r"    (?:nullptr|" + name + r"KernSparseValues),[^\n]*\n", "", remainder)
    fnv = 2166136261
    for value in dense:
        fnv = ((fnv ^ (value & 255)) * 16777619) & 0xFFFFFFFF
    return {"rows": rows, "cols": cols, "matrixFnv32": fnv,
            "matrixSha256": hashlib.sha256(bytes(v & 255 for v in dense)).hexdigest(),
            "remainderSha256": hashlib.sha256(remainder.encode()).hexdigest()}


class BuiltinKerningTest(unittest.TestCase):
    def test_all_migrated_fonts_preserve_every_cell_and_other_font_data(self):
        golden = json.loads(Path(__file__).with_name("builtin_kerning_golden.json").read_text())
        self.assertEqual(len(golden), 24)
        for name, expected in golden.items():
            with self.subTest(font=name):
                text = (ROOT / "lib/EpdFont/builtinFonts" / name).read_text(encoding="utf-8")
                self.assertEqual(snapshot(text), expected)
                self.assertNotIn("KernMatrix[]", text)
                self.assertEqual(COMPACT.compact_header(text), (text, 0))


if __name__ == "__main__":
    unittest.main()
