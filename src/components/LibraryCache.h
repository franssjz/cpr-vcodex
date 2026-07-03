#pragma once

#include <string>
#include <vector>

#include "components/themes/BaseTheme.h"  // for Rect

class GfxRenderer;

// Persistent cache for the Library activity. The cache is the source of
// truth for the on-device library view -- the SD card is only walked on cold
// open, manual refresh, or after an inbound transfer invalidates the cache.
//
// Storage layout:
//   /.crosspoint/library.bin v2
//     u8  version (== 2)
//     u16 count
//     repeat `count` records:
//       u16 + bytes : path           (required, non-empty)
//       u16 + bytes : title          (display label for grid tiles)
//       u16 + bytes : author         (empty when unknown)
//
// Records are written in display order so loaders never re-sort.
namespace LibraryCache {

// Slim in-RAM record. Cover thumbnail path is computed on demand from
// `path` via a deterministic hash -- never stored in the cache file.
struct Entry {
  std::string path;
  std::string title;
  std::string author;
};

// Compute the thumbnail BMP path for an entry at the given grid cell size.
std::string thumbPathFor(const std::string& path, int coverW, int coverH);

// Generate a cover thumbnail at the exact grid dimensions. Short-circuits
// when the BMP file already exists.
// Returns true if a thumb is now available on disk.
bool generateCoverForBook(const std::string& path, int coverW, int coverH);

// True iff the cache file exists on disk.
bool exists();

// Read every record from the cache file into `out`. Returns true on success.
bool load(std::vector<Entry>& out);

// Overwrite the cache file with `entries` in their current order.
bool save(const std::vector<Entry>& entries);

// Delete the cache file. Idempotent.
void invalidate();

// Remove the entry whose `path` matches and rewrite the cache file.
bool removeBook(const std::string& path);

// Incremental sync: compare the cached library against the real SD content.
// - Removes entries whose files no longer exist on the SD card.
// - Parses metadata only for newly discovered books and appends them.
// - Re-sorts and persists the cache when changes are detected.
// Falls back to a full scan() when the cache file is missing or read fails.
//
// `out` receives the complete, sorted entry list after sync.
// Progress is shown via `popupRect` only when a full scan fallback occurs;
// incremental sync is fast and runs without a popup.
bool sync(std::vector<Entry>& out, int maxBooks = 300);

// Full SD walk: enumerate every ebook under the card, parse metadata via
// the cheap `Epub::load(true, true)` or `Xtc::load()` / `Txt::load()` path,
// sort, and persist to the cache file. `out` receives the sorted list.
// Cover thumbnails are NOT generated here — they are created on-demand
// per visible page via `generateCoverForBook()`.
//
// Progress is reported by filling `popupRect` — the caller is responsible
// for drawing the surrounding popup chrome before invoking. `maxBooks`
// caps the result.
bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out, int maxBooks = 300);

}  // namespace LibraryCache
