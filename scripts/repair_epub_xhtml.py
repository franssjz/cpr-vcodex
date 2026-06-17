#!/usr/bin/env python3
"""Repair common non-XHTML void tags inside an EPUB.

CPR-vCodex parses chapter files with Expat, so EPUB spine documents must be
well-formed XML/XHTML. Some "optimized" EPUBs contain HTML-style void tags such
as <meta charset="utf-8">, which browsers tolerate but Expat rejects.
"""

from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path


VOID_TAGS = ("area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr")
VOID_TAG_RE = re.compile(
    rb"<(?P<tag>" + b"|".join(tag.encode("ascii") for tag in VOID_TAGS) + rb")\b(?P<attrs>[^<>]*?)(?<!/)>",
    re.IGNORECASE,
)


def is_markup_entry(name: str) -> bool:
    lower = name.lower()
    return lower.endswith((".html", ".xhtml", ".htm"))


def repair_void_tags(data: bytes) -> tuple[bytes, int]:
    replacements = 0

    def replace(match: re.Match[bytes]) -> bytes:
        nonlocal replacements
        attrs = match.group("attrs").rstrip()
        if attrs.endswith(b"/"):
            return match.group(0)
        replacements += 1
        return b"<" + match.group("tag") + attrs + b" />"

    return VOID_TAG_RE.sub(replace, data), replacements


def main() -> int:
    parser = argparse.ArgumentParser(description="Repair common non-XHTML void tags in an EPUB copy.")
    parser.add_argument("input", type=Path, help="Input .epub")
    parser.add_argument("output", type=Path, help="Output repaired .epub")
    args = parser.parse_args()

    if not args.input.exists():
        print(f"ERROR: input does not exist: {args.input}", file=sys.stderr)
        return 2
    if args.input.resolve() == args.output.resolve():
        print("ERROR: output must be a different file; refusing to overwrite input in place", file=sys.stderr)
        return 2

    total_replacements = 0
    touched_files = 0

    with zipfile.ZipFile(args.input, "r") as src, zipfile.ZipFile(args.output, "w") as dst:
        entries = src.infolist()

        mimetype = next((entry for entry in entries if entry.filename == "mimetype"), None)
        if mimetype:
            dst.writestr("mimetype", src.read("mimetype"), compress_type=zipfile.ZIP_STORED)

        for entry in entries:
            if entry.filename == "mimetype":
                continue

            data = src.read(entry.filename)
            replacements = 0
            if is_markup_entry(entry.filename):
                data, replacements = repair_void_tags(data)
                if replacements:
                    touched_files += 1
                    total_replacements += replacements

            info = zipfile.ZipInfo(entry.filename, date_time=entry.date_time)
            info.comment = entry.comment
            info.extra = entry.extra
            info.internal_attr = entry.internal_attr
            info.external_attr = entry.external_attr
            info.create_system = entry.create_system
            info.compress_type = entry.compress_type
            dst.writestr(info, data)

    print(f"Wrote: {args.output}")
    print(f"Updated markup files: {touched_files}")
    print(f"Void tags self-closed: {total_replacements}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
