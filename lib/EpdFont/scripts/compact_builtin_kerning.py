"""Losslessly migrate existing generated headers to fontconvert.py's sparse kerning.

This avoids rerasterizing fonts (and changing glyph metrics) just to compact their
tables. SD-card fonts and their on-disk format are untouched. Run with header paths;
without --write this only reports savings. Already sparse headers are unchanged.
"""

import argparse
from pathlib import Path
import re


def compact_header(text):
    matrix = re.search(r"static const int8_t (\w+)KernMatrix\[\] = \{(.*?)\n\};", text, re.S)
    if not matrix:
        return text, 0
    name = matrix[1]
    fields = re.search(
        rf"    {name}KernMatrix,\n"
        r"    nullptr,  // kernRowOffsets[^\n]*\n"
        r"    nullptr,  // kernSparseCols\n"
        r"    nullptr,  // kernSparseValues\n"
        r"    (\d+),\n    (\d+),\n    (\d+),\n    (\d+),",
        text,
    )
    if not fields:
        raise ValueError(f"Unsupported descriptor for {name}")
    rows, cols = int(fields[3]), int(fields[4])
    dense = [int(v.strip()) for v in matrix[2].split(",") if v.strip()]
    if not (0 < rows <= 255 and 0 < cols <= 255) or len(dense) != rows * cols:
        raise ValueError(f"Invalid matrix dimensions for {name}")
    if any(v < -128 or v > 127 for v in dense):
        raise ValueError(f"Invalid int8 kerning for {name}")
    offsets, columns, values = [0], [], []
    for row in range(rows):
        for col in range(cols):
            value = dense[row * cols + col]
            if value:
                columns.append(col)
                values.append(value)
        offsets.append(len(values))
    if len(values) > 65535:
        raise ValueError(f"Too many sparse entries for {name}")
    saved = len(dense) - 2 * (len(offsets) + len(values))
    if saved <= 0:
        return text, 0

    # Verify every cell, including implicit zeros, before changing the header.
    restored = [0] * len(dense)
    for row in range(rows):
        for i in range(offsets[row], offsets[row + 1]):
            restored[row * cols + columns[i]] = values[i]
    if restored != dense:
        raise ValueError(f"Kerning round trip failed for {name}")

    def array(kind, suffix, items):
        lines = [f"static const {kind} {name}{suffix}[] = {{"]
        for start in range(0, len(items), 16):
            lines.append("    " + ", ".join(str(v) for v in items[start:start + 16]) + ",")
        return "\n".join([*lines, "};"])

    tables = "\n\n".join((array("uint16_t", "KernRowOffsets", offsets),
                            array("uint8_t", "KernSparseCols", columns),
                            array("int8_t", "KernSparseValues", values)))
    descriptor = (
        "    nullptr,  // kernMatrix: losslessly compacted by compact_builtin_kerning.py\n"
        f"    {name}KernRowOffsets,\n    {name}KernSparseCols,\n    {name}KernSparseValues,\n"
        + "\n".join(f"    {fields[i]}," for i in range(1, 5))
    )
    result = text[:fields.start()] + descriptor + text[fields.end():]
    result = result[:matrix.start()] + tables + result[matrix.end():]
    return result, saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("headers", nargs="+", type=Path)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    total = 0
    for path in args.headers:
        original = path.read_text(encoding="utf-8")
        compact, saved = compact_header(original)
        if saved:
            print(f"{path.name}: {saved:,} bytes saved")
            if args.write:
                with path.open("w", encoding="utf-8", newline="\n") as output:
                    output.write(compact)
            total += saved
    print(f"Total table savings: {total:,} bytes")


if __name__ == "__main__":
    main()
