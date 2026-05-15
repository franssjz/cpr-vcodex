#!/usr/bin/env python3
"""Inspect an EPUB for CPR-vCodex first-load risk factors.

This is a host-side triage tool. It does not execute the firmware parser, but it
checks the structures and entry sizes that the device walks while building the
/.crosspoint EPUB cache.
"""

from __future__ import annotations

import argparse
import posixpath
import re
import sys
import time
import zipfile
from pathlib import Path
from xml.etree import ElementTree


MAX_CSS_SIZE = 128 * 1024
LARGE_XHTML_SIZE = 512 * 1024
LARGE_IMAGE_SIZE = 4 * 1024 * 1024
HUGE_ENTRY_COUNT = 2000
LONG_TEXT_NODE = 16 * 1024

IMAGE_REF_RE = re.compile(rb"""<img\b[^>]*\bsrc\s*=\s*["']([^"']+)["']""", re.IGNORECASE)


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def attr_value(element: ElementTree.Element, name: str) -> str:
    for key, value in element.attrib.items():
        if local_name(key) == name:
            return value
    return ""


def read_xml(zf: zipfile.ZipFile, name: str) -> ElementTree.Element:
    with zf.open(name) as file:
        return ElementTree.fromstring(file.read())


def normalize_zip_path(base: str, href: str) -> str:
    if not href:
        return ""
    href = href.split("#", 1)[0]
    return posixpath.normpath(posixpath.join(base, href)).lstrip("/")


def find_container_rootfile(zf: zipfile.ZipFile) -> str:
    root = read_xml(zf, "META-INF/container.xml")
    for element in root.iter():
        if local_name(element.tag) == "rootfile":
            full_path = element.attrib.get("full-path", "")
            if full_path:
                return full_path
    return ""


def is_xhtml(media_type: str, href: str) -> bool:
    lower_href = href.lower()
    return (
        media_type in {"application/xhtml+xml", "text/html"}
        or lower_href.endswith((".xhtml", ".html", ".htm"))
    )


def is_css(media_type: str, href: str) -> bool:
    return media_type == "text/css" or href.lower().endswith(".css")


def is_image(media_type: str, href: str) -> bool:
    return media_type.startswith("image/") or href.lower().endswith((".jpg", ".jpeg", ".png", ".gif", ".webp"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect an EPUB for CPR-vCodex load/cache-build risk factors.")
    parser.add_argument("epub", type=Path, help="Path to the .epub file")
    parser.add_argument("--top", type=int, default=12, help="Number of largest ZIP entries to print")
    parser.add_argument("--spine", action="store_true", help="Print all spine entries with inflated sizes")
    parser.add_argument(
        "--deep",
        action="store_true",
        help="Parse XHTML spine entries and check image references, XML validity, and long text nodes",
    )
    args = parser.parse_args()

    epub_path = args.epub
    if not epub_path.exists():
        print(f"ERROR: file does not exist: {epub_path}", file=sys.stderr)
        return 2

    started = time.perf_counter()
    warnings: list[str] = []

    try:
        with zipfile.ZipFile(epub_path) as zf:
            bad_entry = zf.testzip()
            if bad_entry:
                print(f"ERROR: ZIP CRC check failed at {bad_entry}")
                return 1

            entries = zf.infolist()
            names = {entry.filename for entry in entries}
            total_uncompressed = sum(entry.file_size for entry in entries)
            total_compressed = sum(entry.compress_size for entry in entries)

            print(f"EPUB: {epub_path}")
            print(f"ZIP entries: {len(entries)}")
            print(f"Compressed size: {total_compressed:,} bytes")
            print(f"Uncompressed size: {total_uncompressed:,} bytes")

            if len(entries) > HUGE_ENTRY_COUNT:
                warnings.append(f"Very high ZIP entry count: {len(entries)}")
            if "META-INF/container.xml" not in names:
                print("ERROR: missing META-INF/container.xml")
                return 1

            opf_path = find_container_rootfile(zf)
            if not opf_path:
                print("ERROR: container.xml has no rootfile full-path")
                return 1
            if opf_path not in names:
                print(f"ERROR: OPF path from container.xml is missing: {opf_path}")
                return 1

            opf_base = posixpath.dirname(opf_path)
            opf_base = f"{opf_base}/" if opf_base else ""
            opf_root = read_xml(zf, opf_path)
            print(f"OPF: {opf_path}")

            manifest: dict[str, dict[str, str]] = {}
            spine_ids: list[str] = []
            nav_path = ""
            ncx_path = ""
            title = ""
            language = ""

            for element in opf_root.iter():
                tag = local_name(element.tag)
                if tag == "title" and not title and element.text:
                    title = element.text.strip()
                elif tag == "language" and not language and element.text:
                    language = element.text.strip()
                elif tag == "item":
                    item_id = element.attrib.get("id", "")
                    href = element.attrib.get("href", "")
                    media_type = element.attrib.get("media-type", "")
                    properties = element.attrib.get("properties", "")
                    full_path = normalize_zip_path(opf_base, href)
                    manifest[item_id] = {"href": href, "path": full_path, "media": media_type}
                    if "nav" in properties.split():
                        nav_path = full_path
                    if media_type == "application/x-dtbncx+xml":
                        ncx_path = full_path
                elif tag == "itemref":
                    idref = element.attrib.get("idref", "")
                    if idref:
                        spine_ids.append(idref)
                elif tag == "spine":
                    toc_id = element.attrib.get("toc", "")
                    if toc_id and toc_id in manifest:
                        ncx_path = manifest[toc_id]["path"]

            print(f"Title: {title or '(none found)'}")
            print(f"Language: {language or '(none found)'}")
            print(f"Manifest items: {len(manifest)}")
            print(f"Spine items: {len(spine_ids)}")
            print(f"EPUB 3 nav: {nav_path or '(none)'}")
            print(f"EPUB 2 NCX: {ncx_path or '(none)'}")

            missing_manifest_paths = [item["path"] for item in manifest.values() if item["path"] and item["path"] not in names]
            missing_spine = [idref for idref in spine_ids if idref not in manifest]
            if missing_manifest_paths:
                warnings.append(f"Manifest references missing ZIP files: {len(missing_manifest_paths)}")
            if missing_spine:
                warnings.append(f"Spine references missing manifest IDs: {len(missing_spine)}")
            if not nav_path and not ncx_path:
                warnings.append("No EPUB 3 nav or EPUB 2 NCX TOC found")

            css_items = [item for item in manifest.values() if is_css(item["media"], item["href"])]
            xhtml_items = [item for item in manifest.values() if is_xhtml(item["media"], item["href"])]
            image_items = [item for item in manifest.values() if is_image(item["media"], item["href"])]

            print(f"CSS files: {len(css_items)}")
            for item in css_items:
                path = item["path"]
                if path in names:
                    size = zf.getinfo(path).file_size
                    if size > MAX_CSS_SIZE:
                        warnings.append(f"CSS over firmware parse limit ({size:,} bytes): {path}")

            print(f"XHTML/HTML files: {len(xhtml_items)}")
            for item in xhtml_items:
                path = item["path"]
                if path in names:
                    size = zf.getinfo(path).file_size
                    if size > LARGE_XHTML_SIZE:
                        warnings.append(f"Large XHTML/HTML entry ({size:,} bytes): {path}")

            print(f"Image files: {len(image_items)}")
            for item in image_items:
                path = item["path"]
                if path in names:
                    size = zf.getinfo(path).file_size
                    if size > LARGE_IMAGE_SIZE:
                        warnings.append(f"Large image entry ({size:,} bytes): {path}")

            spine_paths = [manifest[idref]["path"] for idref in spine_ids if idref in manifest]

            if args.spine:
                print("\nSpine entries:")
                cumulative_size = 0
                for index, path in enumerate(spine_paths):
                    size = zf.getinfo(path).file_size if path in names else 0
                    cumulative_size += size
                    print(f"  {index:>2}: {size:>8,} bytes  cumulative {cumulative_size:>9,}  {path}")

            if args.deep:
                print("\nDeep spine scan:")
                for index, path in enumerate(spine_paths):
                    if path not in names:
                        continue
                    data = zf.read(path)
                    image_refs = [ref.decode("utf-8", errors="replace") for ref in IMAGE_REF_RE.findall(data)]
                    missing_images = []
                    for ref in image_refs:
                        image_path = normalize_zip_path(posixpath.dirname(path) + "/", ref)
                        if image_path and image_path not in names:
                            missing_images.append(ref)
                    try:
                        root = ElementTree.fromstring(data)
                        longest_text = max((len(text) for text in root.itertext() if text), default=0)
                    except ElementTree.ParseError as error:
                        warnings.append(f"XML parse failed in spine {index} ({path}): {error}")
                        longest_text = 0

                    if longest_text > LONG_TEXT_NODE:
                        warnings.append(f"Very long text node in spine {index} ({longest_text:,} chars): {path}")
                    if missing_images:
                        warnings.append(f"Missing image refs in spine {index} ({path}): {', '.join(missing_images[:5])}")

                    print(
                        f"  {index:>2}: {len(data):>8,} bytes  images {len(image_refs):>2}"
                        f"  longest text {longest_text:>6,}  {path}"
                    )

            print(f"\nLargest {args.top} entries:")
            for entry in sorted(entries, key=lambda item: item.file_size, reverse=True)[: args.top]:
                ratio = entry.file_size / max(entry.compress_size, 1)
                print(f"  {entry.file_size:>12,} bytes  ratio {ratio:>6.1f}  {entry.filename}")

    except zipfile.BadZipFile as error:
        print(f"ERROR: invalid ZIP/EPUB: {error}")
        return 1
    except ElementTree.ParseError as error:
        print(f"ERROR: XML parse failed: {error}")
        return 1
    except KeyError as error:
        print(f"ERROR: missing required ZIP entry: {error}")
        return 1

    elapsed_ms = (time.perf_counter() - started) * 1000.0
    print(f"\nInspection time: {elapsed_ms:.0f} ms")
    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            print(f"  - {warning}")
        return 1

    print("\nNo obvious first-load risk factors found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
