#!/usr/bin/env python3
"""
check_covers.py  —  Verify cover thumbnails for EPUB/XTC books on the SD card.

Usage:
    python scripts/check_covers.py <SD_CARD_PATH>
    python scripts/check_covers.py E:\\                    # Windows
    python scripts/check_covers.py /media/user/SD_CARD    # Linux / macOS

    # Only show books added/modified in the last N days (useful after OPDS downloads):
    python scripts/check_covers.py E:\\ --recent 3

How it works:
    cpr-vcodex stores each book's cached cover at:
        <SD>/.crosspoint/epub_<hash>/thumb_<W>x<H>.bmp   (Lyra Carousel)
        <SD>/.crosspoint/epub_<hash>/thumb_<H>.bmp        (other themes)

    where <hash> = std::to_string(std::hash<std::string>{}(filepath))
    on ESP32-C3 (32-bit RISC-V, GCC libstdc++ Murmur-2, seed=0xC70F6907).

    This script replicates that hash so it can map every EPUB file directly
    to its expected cache directory and check whether the thumb exists.
"""

import os
import sys
import struct
from datetime import datetime, timedelta

# ─────────────────────────────────────────────────────────────────────────────
# GCC libstdc++ _Hash_bytes — 32-bit Murmur-2 variant
# Matches std::hash<std::string>{} on ESP32-C3 (RISC-V 32-bit, little-endian).
# ─────────────────────────────────────────────────────────────────────────────
def _hash_bytes_32(data: bytes, seed: int = 0xC70F6907) -> int:
    M = 0x5BD1E995
    MASK = 0xFFFFFFFF
    h = (seed ^ len(data)) & MASK
    i = 0
    while i + 4 <= len(data):
        (k,) = struct.unpack_from("<I", data, i)
        k = (k * M) & MASK
        k ^= k >> 24
        k = (k * M) & MASK
        h = (h * M) & MASK
        h ^= k
        i += 4
    rem = len(data) - i
    if rem >= 3:
        h ^= data[i + 2] << 16
    if rem >= 2:
        h ^= data[i + 1] << 8
    if rem >= 1:
        h ^= data[i]
        h = (h * M) & MASK
    h ^= h >> 13
    h = (h * M) & MASK
    h ^= h >> 15
    return h


def epub_cache_dir_name(device_path: str) -> str:
    """Return the expected cache directory name for an on-device EPUB path."""
    return f"epub_{_hash_bytes_32(device_path.encode('utf-8'))}"


def to_device_path(sd_root: str, host_path: str) -> str:
    """Convert a host absolute path to the on-device path (forward slashes, SD-relative)."""
    rel = os.path.relpath(host_path, sd_root)
    return "/" + rel.replace(os.sep, "/")


# ─────────────────────────────────────────────────────────────────────────────
# Known thumb filenames (from LyraCarouselTheme.h / LyraTheme.h)
# ─────────────────────────────────────────────────────────────────────────────
CAROUSEL_THUMB = "thumb_340x540.bmp"   # kCenterCoverW=340, kCenterCoverH=540
LEGACY_CAROUSEL = "thumb_600.bmp"      # LyraCarouselMetrics::homeCoverHeight=600
STANDARD_LYRA = "thumb_226.bmp"        # LyraMetrics::homeCoverHeight=226

EXTENSIONS = (".epub", ".xtc")


# ─────────────────────────────────────────────────────────────────────────────

def find_books(sd_root: str, only_recent_days: int = 0) -> list:
    results = []
    cutoff = datetime.now() - timedelta(days=only_recent_days) if only_recent_days else None
    for dirpath, dirnames, filenames in os.walk(sd_root):
        dirnames[:] = [d for d in dirnames if d != ".crosspoint"]
        for fname in filenames:
            if fname.lower().endswith(EXTENSIONS):
                full = os.path.join(dirpath, fname)
                if cutoff and datetime.fromtimestamp(os.path.getmtime(full)) < cutoff:
                    continue
                results.append(full)
    return sorted(results)


def check_sd(sd_root: str, only_recent_days: int = 0) -> None:
    crosspoint = os.path.join(sd_root, ".crosspoint")
    if not os.path.isdir(crosspoint):
        print(f"❌  No .crosspoint directory found at: {sd_root}")
        print("    Has the device been powered on at least once with this SD card?")
        return

    books = find_books(sd_root, only_recent_days)
    if not books:
        label = f"modified in the last {only_recent_days} day(s)" if only_recent_days else "on the SD card"
        print(f"No EPUB/XTC files found {label}.")
        return

    scope = f"(last {only_recent_days}d)" if only_recent_days else ""
    print(f"Found {len(books)} book(s) {scope}\n")

    W_STATUS = 14
    W_CACHE  = 18
    W_THUMBS = 32
    header = f"{'STATUS':<{W_STATUS}} {'CACHE DIR':<{W_CACHE}} {'THUMBS FOUND':<{W_THUMBS}} PATH"
    print(header)
    print("─" * 100)

    counts = {"ok": 0, "no_cache": 0, "no_thumb": 0}

    for host_path in books:
        dev_path = to_device_path(sd_root, host_path)
        cache_name = epub_cache_dir_name(dev_path)
        cache_dir = os.path.join(crosspoint, cache_name)

        if not os.path.isdir(cache_dir):
            status = "⚠ NO CACHE"
            thumbs_str = "—"
            counts["no_cache"] += 1
        else:
            all_thumbs = sorted(
                f for f in os.listdir(cache_dir)
                if f.startswith("thumb_") and f.endswith(".bmp")
            )
            # Separate real thumbs (>0 bytes) from empty sentinels (0 bytes = permanent decode failure)
            real_thumbs = [f for f in all_thumbs if os.path.getsize(os.path.join(cache_dir, f)) > 0]
            sentinels   = [f for f in all_thumbs if os.path.getsize(os.path.join(cache_dir, f)) == 0]

            if not all_thumbs:
                status = "❌ NO THUMB"
                thumbs_str = "—"
                counts["no_thumb"] += 1
            elif real_thumbs:
                has_carousel = CAROUSEL_THUMB in real_thumbs
                status = "✅ OK" if has_carousel else "⚠ LEGACY ONLY"
                counts["ok" if has_carousel else "no_thumb"] += 1
                thumbs_str = ", ".join(real_thumbs)
            else:
                # Only sentinels — permanent decode failure (e.g. progressive JPEG)
                status = "🚫 SENTINEL"
                thumbs_str = f"sentinel ({', '.join(sentinels)})"
                counts["no_thumb"] += 1

        max_path = 100 - W_STATUS - W_CACHE - W_THUMBS - 3
        short = dev_path if len(dev_path) <= max_path else "…" + dev_path[-(max_path - 1):]
        print(f"{status:<{W_STATUS}} {cache_name:<{W_CACHE}} {thumbs_str:<{W_THUMBS}} {short}")

    print("─" * 100)
    print(
        f"\nResult: {counts['ok']} ✅ OK  |  "
        f"{counts['no_cache']} ⚠ no cache dir  |  "
        f"{counts['no_thumb']} ❌ missing carousel thumb\n"
    )

    if counts["no_cache"]:
        print("⚠ NO CACHE — book was never opened on-device; open it once to generate the thumb.")
    if counts["no_thumb"]:
        print("❌ NO THUMB / LEGACY ONLY — thumb generation failed or used an old size.")
        print("   Delete the cache dir for that book and open it again on the device.")


def _print_usage() -> None:
    print(__doc__)


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        _print_usage()
        sys.exit(0 if args else 1)

    sd = args[0].rstrip("\\/")

    recent = 0
    if "--recent" in args:
        idx = args.index("--recent")
        try:
            recent = int(args[idx + 1])
        except (IndexError, ValueError):
            print("--recent requires a number of days, e.g.:  --recent 3")
            sys.exit(1)

    check_sd(sd, only_recent_days=recent)
