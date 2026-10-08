#include "BookProgress.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>

#include "BookIdentity.h"
#include "ReadingStatsStore.h"

namespace {
uint32_t readLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

int loadBookProgress(const std::string& path) {
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    const auto* stats = READING_STATS.findMatchingBookForPath(path);
    return stats ? std::min(100, static_cast<int>(stats->lastProgressPercent)) : -1;
  }
  const std::string bookId = BookIdentity::resolveStableBookId(path);
  const auto openProgress = [&](const char* name, const std::string& cache, HalFile& file) {
    const auto stable = BookIdentity::getStableDataFilePath(bookId, name);
    if (!bookId.empty() && Storage.exists(stable.c_str())) return Storage.openFileForRead("HOME", stable, file);
    return Storage.openFileForRead("HOME", cache + "/progress.bin", file);
  };
  uint8_t data[10]{};
  if (FsHelpers::hasEpubExtension(path)) {
    // Metadata objects exceed the stack budget; only the featured book is loaded, once per entry.
    auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: progress metadata");
      return -1;
    }
    if (!epub->load(false, true)) return -1;
    HalFile file;
    if (!openProgress("epub_progress.bin", epub->getCachePath(), file)) return -1;
    const int size = file.read(data, sizeof(data));
    if (size != 4 && size != 6 && size != 10) return -1;
    const int spine = data[0] | (data[1] << 8);
    const int page = data[2] | (data[3] << 8);
    const int total = size >= 6 ? data[4] | (data[5] << 8) : 0;
    if (epub->getSpineItemsCount() <= 0 || epub->getBookSize() == 0) return -1;
    if (spine == epub->getSpineItemsCount()) return 100;
    if (spine > epub->getSpineItemsCount()) return -1;
    const float fraction = total > 0 && page != UINT16_MAX ? std::clamp(float(page) / total, 0.0f, 1.0f) : 0;
    return std::clamp(static_cast<int>(epub->calculateProgress(spine, fraction) * 100 + 0.5f), 0, 100);
  }
  if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: XTC progress metadata");
      return -1;
    }
    if (!xtc->load()) return -1;
    HalFile file;
    if (!openProgress("xtc_progress.bin", xtc->getCachePath(), file) || file.read(data, 4) != 4) return -1;
    const uint32_t page = readLe32(data);
    if (xtc->getPageCount() == 0) return -1;
    if (page >= xtc->getPageCount()) return 100;
    return xtc->calculateProgress(page);
  }
  return -1;
}
