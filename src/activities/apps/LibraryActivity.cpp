#include "LibraryActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

#include "../home/BookContextMenuActivity.h"
#include "../home/BookMetadataActivity.h"
#include "../util/ConfirmationActivity.h"
#include "CrossPointSettings.h"
#include "Epub.h"
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/icons/heart.h"
#include "util/CoverRibbonBaker.h"
#include "Txt.h"
#include "Xtc.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {
constexpr int COVER_CORNER_RADIUS = 2;
constexpr const char* LIBRARY_INVENTORY_FILE = "/.crosspoint/library_inventory.json";
constexpr unsigned long INVENTORY_STALE_MS = 30000;  // re-scan if inventory older than 30s

// Priority-based ribbon overlay (top-right corner).
// Only ONE ribbon shown per cover, highest priority wins.
// Priority: 1=Read(completed), 2=Favorite, 3=Opened.
static void fillTopRightTri(GfxRenderer& r, int x, int y, int leg, bool black) {
  for (int dy = 0; dy < leg; ++dy)
    r.fillRect(x + dy, y + dy, leg - dy, 1, black);
}

void drawRibbonBadge(GfxRenderer& r, int cx, int cy, int cw, int ch,
                     bool completed, bool favorite, bool opened) {
  (void)ch;
  const int leg = std::max(22, std::min(cw * 8 / 10, 84));
  const int rx = cx + cw - leg;
  const int ry = cy;

  fillTopRightTri(r, rx - 3, ry - 3, leg + 6, false);
  fillTopRightTri(r, rx - 2, ry - 2, leg + 4, true);
  fillTopRightTri(r, rx - 1, ry - 1, leg + 2, false);
  fillTopRightTri(r, rx,     ry,     leg,     true);

  const int symCx = cx + cw - leg / 3;
  const int symCy = cy + leg / 3;
  const int symSz = std::max(8, leg * 22 / 100);
  const int symX = symCx - symSz / 2;
  const int symY = symCy - symSz / 2;

  if (completed) {
    r.drawLine(symCx - 5, symCy,     symCx - 1, symCy + 4, 2, false);
    r.drawLine(symCx - 1, symCy + 4, symCx + 6, symCy - 4, 2, false);
  } else if (favorite) {
    // HeartIcon is 32×32; draw at native size centered on symCx/symCy
    constexpr int kHeartNativeSz = 32;
    if (leg >= kHeartNativeSz) {
      int hx = symCx - kHeartNativeSz / 2;
      int hy = symCy - kHeartNativeSz / 2;
      r.drawIconInverted(::HeartIcon, hx, hy, kHeartNativeSz, kHeartNativeSz);
    }
  } else if (opened) {
    const int dotR = std::max(1, symSz / 4);
    for (int y2 = -dotR; y2 <= dotR; ++y2)
      for (int x2 = -dotR; x2 <= dotR; ++x2)
        if (x2 * x2 + y2 * y2 <= dotR * dotR + dotR)
          r.drawLine(symCx + x2, symCy + y2, symCx + x2, symCy + y2, 1, false);
  }
}

std::string filenameWithoutExtension(const std::string& path) {
  std::string name = path;
  const size_t lastSlash = name.find_last_of('/');
  if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);
  const size_t lastDot = name.find_last_of('.');
  if (lastDot != std::string::npos && lastDot > 0) name = name.substr(0, lastDot);
  return name;
}

inline bool isEbookExtension(std::string_view filename) {
  return FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
         FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename);
}

std::string libraryCoverDir() { return "/.crosspoint/library_covers"; }

// Directories that should always be skipped during library scanning.
// These are common system / app directories that never contain user ebooks.
bool isExcludedDirectory(const std::string& dirName) {
  // Dot-prefixed (hidden) directories
  if (!dirName.empty() && dirName[0] == '.') return true;
  // System / app directories (case-insensitive)
  std::string lower = dirName;
  for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (lower == "crosspoint") return true;
  if (lower == "sleep" || lower.compare(0, 5, "sleep") == 0) return true;  // sleep, sleep*
  if (lower == "font" || lower == "fonts") return true;
  if (lower == "dictionaries") return true;
  if (lower == "exports") return true;
  if (lower == "system volume information") return true;
  return false;
}

void cleanupZeroSizeThumbs() {
  auto d = Storage.open(libraryCoverDir().c_str());
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return;
  }
  d.rewindDirectory();
  char nb[256];
  for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
    f.getName(nb, sizeof(nb));
    std::string full = libraryCoverDir() + "/" + nb;
    if (!f.isDirectory() && f.size() == 0) {
      f.close();
      Storage.remove(full.c_str());
      LOG_DBG("LIB", "Removed zero-size thumb: %s", full.c_str());
    } else {
      f.close();
    }
  }
  d.close();
}

// Periodic cleanup: run at most once per day.
void runPeriodicCleanupIfNeeded() {
  uint32_t refTs = TimeUtils::getAuthoritativeTimestamp();
  if (!TimeUtils::isClockValid(refTs)) return;
  uint32_t today = TimeUtils::getLocalDayOrdinal(refTs);
  if (today == 0) return;
  uint8_t today8 = static_cast<uint8_t>(today % 365);  // day-of-year
  if (today8 == SETTINGS.libraryLastCleanupDay) return;
  SETTINGS.libraryLastCleanupDay = today8;
  SETTINGS.saveToFile();
  cleanupZeroSizeThumbs();
}

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  return h;
}

std::string libraryCoverPathFor(const std::string& bookPath, int w, int h) {
  char buf[80];
  snprintf(buf, sizeof(buf), "%s/%08lx_%dx%d.bmp", libraryCoverDir().c_str(),
           static_cast<unsigned long>(fnv1a(bookPath)), w, h);
  return buf;
}

// ----- Inventory caching -----
bool saveInventoryToFile(const std::vector<LibraryEntry>& entries) {
  // Write a simple newline-delimited list: path|title
  HalFile file;
  if (!Storage.openFileForWrite("LIB", LIBRARY_INVENTORY_FILE, file)) return false;
  for (const auto& e : entries) {
    std::string line = e.path + "|" + e.title + "\n";
    if (file.write(line.c_str(), line.size()) != line.size()) {
      file.close();
      Storage.remove(LIBRARY_INVENTORY_FILE);
      return false;
    }
  }
  file.close();
  return true;
}

bool loadInventoryFromFile(std::vector<LibraryEntry>& entries, unsigned long& outTimestamp) {
  HalFile file;
  if (!Storage.openFileForRead("LIB", LIBRARY_INVENTORY_FILE, file)) return false;
  outTimestamp = static_cast<unsigned long>(file.fileSize64());  // use size as proxy timestamp
  // Actually we need a proper timestamp. Read first line as comment with timestamp.
  // Simpler: check file modification via separate timestamp file, or just use file size.
  // For now: if file exists, treat as recent enough. We use a fixed invalidation approach.
  entries.clear();
  // Read the whole file as string
  size_t sz = file.size();
  if (sz == 0 || sz > 65536) { file.close(); return false; }
  std::vector<char> buf(sz + 1);
  size_t read = file.read(buf.data(), sz);
  file.close();
  if (read != sz) return false;
  buf[sz] = '\0';

  std::string content(buf.data(), sz);
  // First line: timestamp
  size_t nlPos = content.find('\n');
  if (nlPos == std::string::npos) return false;
  std::string tsStr = content.substr(0, nlPos);
  outTimestamp = static_cast<unsigned long>(strtoul(tsStr.c_str(), nullptr, 10));

  // Remaining lines: path|title
  size_t pos = nlPos + 1;
  while (pos < content.size()) {
    size_t end = content.find('\n', pos);
    if (end == std::string::npos) end = content.size();
    std::string line = content.substr(pos, end - pos);
    pos = end + 1;
    if (line.empty()) continue;
    size_t pipePos = line.find('|');
    LibraryEntry e;
    e.path = line.substr(0, pipePos);
    if (pipePos != std::string::npos) {
      e.title = line.substr(pipePos + 1);
    }
    if (!e.path.empty()) {
      entries.push_back(std::move(e));
    }
  }
  return true;
}

bool isInventoryStale(unsigned long invTimestamp) {
  // Use system uptime as approximate clock for staleness check
  if (invTimestamp == 0) return true;
  unsigned long now = millis();
  return (now - invTimestamp) > INVENTORY_STALE_MS;
}

}  // namespace

std::string LibraryActivity::libraryCoverPath(const std::string& bookPath) const {
  return libraryCoverPathFor(bookPath, coverWidth_, coverHeight_);
}

void LibraryActivity::applyLayoutFromSettings() {
  switch (SETTINGS.libraryLayout) {
    case CrossPointSettings::LIBRARY_LAYOUT_2X2:
      gridColumns_ = 2;
      coverWidth_ = 202;
      coverHeight_ = 310;
      break;
    case CrossPointSettings::LIBRARY_LAYOUT_3X3:
      gridColumns_ = 3;
      coverWidth_ = 130;
      coverHeight_ = 190;
      break;
    case CrossPointSettings::LIBRARY_LAYOUT_4X4:
    default:
      gridColumns_ = 4;
      coverWidth_ = 100;
      coverHeight_ = 150;
      break;
  }
  gridsPerPage_ = gridColumns_ * gridColumns_;
}

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  std::vector<LibraryEntry> allEntries;
  Storage.mkdir(libraryCoverDir().c_str());

  std::function<void(const std::string&)> walk;
  walk = [&](const std::string& dir) {
    auto d = Storage.open(dir.c_str());
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    d.rewindDirectory();
    char nb[256];
    for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
      f.getName(nb, sizeof(nb));
      if (f.isDirectory()) {
        // Skip system/excluded directories entirely
        std::string dirName = nb;
        if (isExcludedDirectory(dirName)) { f.close(); continue; }
        f.close();
        walk(dir + dirName + '/');
      } else if (isEbookExtension(nb)) {
        if (strcmp(nb, "if_found.txt") == 0 || strcmp(nb, "crash_report.txt") == 0) { f.close(); continue; }
        LibraryEntry e;
        e.path = dir + nb;
        e.title = filenameWithoutExtension(e.path);
        // Defer cover existence check: don't call Storage.exists() here
        // (saves ~50% I/O during scan). Cover is resolved lazily in render/loop.
        allEntries.push_back(std::move(e));
      }
      f.close();
    }
    d.close();
  };

  // Use configured library root directory
  const char* rootDir = SETTINGS.libraryRootDir;
  if (rootDir[0] == '\0' || (rootDir[0] == '/' && rootDir[1] == '\0')) {
    // Default: walk from "/" but skip excluded dirs
    // Walk top-level entries, skip excluded dirs
    auto rd = Storage.open("/");
    if (rd && rd.isDirectory()) {
      rd.rewindDirectory();
      char nb[256];
      for (auto f = rd.openNextFile(); f; f = rd.openNextFile()) {
        f.getName(nb, sizeof(nb));
        if (f.isDirectory()) {
          std::string dirName = nb;
          if (isExcludedDirectory(dirName)) { f.close(); continue; }
          f.close();
          walk("/" + dirName + "/");
        } else if (isEbookExtension(nb)) {
          if (strcmp(nb, "if_found.txt") == 0 || strcmp(nb, "crash_report.txt") == 0) { f.close(); continue; }
          LibraryEntry e;
          e.path = "/" + std::string(nb);
          e.title = filenameWithoutExtension(e.path);
          allEntries.push_back(std::move(e));
        }
        f.close();
      }
      rd.close();
    }
  } else {
    // Walk from configured root directory
    std::string root(rootDir);
    // Ensure trailing slash
    if (root.empty() || root.back() != '/') root += '/';
    walk(root);
  }

  std::sort(allEntries.begin(), allEntries.end(), [](auto& a, auto& b) { return a.path < b.path; });

  entries_.clear();
  if (filter == CrossPointSettings::LIBRARY_FILTER_ALL) {
    entries_ = std::move(allEntries);
  } else if (filter == CrossPointSettings::LIBRARY_FILTER_FAVOURITES) {
    for (auto& e : allEntries) {
      if (FAVORITES.isFavorite(e.path)) entries_.push_back(std::move(e));
    }
  } else if (filter == CrossPointSettings::LIBRARY_FILTER_LATEST_READ) {
    const auto& recent = RECENT_BOOKS.getBooks();
    for (auto& e : allEntries) {
      for (const auto& rb : recent) {
        if (rb.path == e.path || (!rb.bookId.empty() && rb.bookId == e.path)) {
          entries_.push_back(std::move(e));
          break;
        }
      }
    }
  }

  currentFilter_ = filter;
  selectorIndex_ = 0;
  coversComplete_ = false;
  coverGenIndex_ = -1;

  // Save inventory cache for next time
  saveInventoryToFile(entries_);
  inventoryLoaded_ = true;
}

void LibraryActivity::scanSd() {
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);

  // Try to load from inventory cache first
  inventoryLoaded_ = false;
  if (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL) {
    unsigned long invTimestamp = 0;
    if (loadInventoryFromFile(entries_, invTimestamp) && !isInventoryStale(invTimestamp)) {
      inventoryLoaded_ = true;
      currentFilter_ = currentFilter_;
      selectorIndex_ = 0;
      coversComplete_ = false;
      coverGenIndex_ = -1;
      LOG_DBG("LIB", "Loaded %d entries from inventory cache", static_cast<int>(entries_.size()));
      return;
    }
  }

  // Full rescan
  rebuildForFilter(currentFilter_);
}

bool LibraryActivity::generateOneCover(const std::string& bookPath, int coverW, int coverH, const std::string& destFile) {
  std::string fname = bookPath;
  size_t slash = fname.find_last_of('/');
  if (slash != std::string::npos) fname = fname.substr(slash + 1);

  bool ok = false;

  if (FsHelpers::hasEpubExtension(fname)) {
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(true, true)) return false;
    if (!epub.generateThumbBmp(coverW, coverH)) return false;
    std::string src = UITheme::getCoverThumbPath(epub.getThumbBmpPath(), coverW, coverH);
    if (src.empty() || !Storage.exists(src.c_str())) return false;
    FsFile fin;
    if (!Storage.openFileForRead("LIB", src, fin)) return false;
    size_t sz = fin.size();
    if (sz == 0) { fin.close(); Storage.remove(src.c_str()); return false; }
    std::vector<uint8_t> buf(sz);
    fin.read(buf.data(), sz);
    fin.close();
    FsFile fout;
    if (!Storage.openFileForWrite("LIB", destFile, fout)) return false;
    size_t written = fout.write(buf.data(), sz);
    fout.close();
    if (written != sz) { Storage.remove(destFile.c_str()); return false; }
    Storage.remove(src.c_str());
    ok = true;
  } else if (FsHelpers::hasXtcExtension(fname)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) return false;
    if (!xtc.generateThumbBmp(coverW, coverH)) return false;
    std::string src = UITheme::getCoverThumbPath(xtc.getThumbBmpPath(), coverW, coverH);
    if (src.empty() || !Storage.exists(src.c_str())) return false;
    FsFile fin;
    if (!Storage.openFileForRead("LIB", src, fin)) return false;
    size_t sz = fin.size();
    if (sz == 0) { fin.close(); Storage.remove(src.c_str()); return false; }
    std::vector<uint8_t> buf(sz);
    fin.read(buf.data(), sz);
    fin.close();
    FsFile fout;
    if (!Storage.openFileForWrite("LIB", destFile, fout)) return false;
    size_t written = fout.write(buf.data(), sz);
    fout.close();
    if (written != sz) { Storage.remove(destFile.c_str()); return false; }
    Storage.remove(src.c_str());
    ok = true;
  } else if (FsHelpers::hasTxtExtension(fname) || FsHelpers::hasMarkdownExtension(fname)) {
    Txt txt(bookPath, "/.crosspoint");
    if (!txt.load()) return false;
    if (!txt.generateCoverBmp()) return false;
    std::string src = txt.getCoverBmpPath();
    if (src.empty() || !Storage.exists(src.c_str())) return false;
    FsFile fin;
    if (!Storage.openFileForRead("LIB", src, fin)) return false;
    size_t sz = fin.size();
    if (sz == 0) { fin.close(); Storage.remove(src.c_str()); return false; }
    std::vector<uint8_t> buf(sz);
    fin.read(buf.data(), sz);
    fin.close();
    FsFile fout;
    if (!Storage.openFileForWrite("LIB", destFile, fout)) return false;
    size_t written = fout.write(buf.data(), sz);
    fout.close();
    if (written != sz) { Storage.remove(destFile.c_str()); return false; }
    Storage.remove(src.c_str());
    ok = true;
  }

  if (ok && Storage.exists(destFile.c_str())) {
    CoverRibbonBaker::bakeIntoFile(destFile, bookPath);
  }
  return ok;
}

void LibraryActivity::generateCoverForEntry(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  LibraryEntry& e = entries_[index];
  if (e.coverFailed) return;
  if (!e.coverPath.empty()) return;
  std::string dest = libraryCoverPath(e.path);
  if (!Storage.exists(dest.c_str())) {
    if (generateOneCover(e.path, coverWidth_, coverHeight_, dest)) e.coverPath = dest;
    else e.coverFailed = true;
  } else {
    e.coverPath = dest;
    CoverRibbonBaker::bakeIntoFile(dest, e.path);
  }
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  applyLayoutFromSettings();
  selectorIndex_ = 0;
  coverGenIndex_ = -1;
  coversComplete_ = false;
  lastPage_ = -1;
  Storage.mkdir(libraryCoverDir().c_str());

  // Periodic cleanup (once per day, not every onEnter)
  runPeriodicCleanupIfNeeded();

  scanSd();
  requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  entries_.clear();
}

void LibraryActivity::loop() {
  const int total = static_cast<int>(entries_.size());

  // Generate missing covers for CURRENT page only, one per loop
  if (!coversComplete_ && total > 0) {
    int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    int pageCount = std::min(gridsPerPage_, total - pageStart);
    int pageEnd = pageStart + pageCount;

    if (coverGenIndex_ < pageStart || coverGenIndex_ >= pageEnd) {
      coverGenIndex_ = pageStart;
      while (coverGenIndex_ < pageEnd) {
        if (entries_[coverGenIndex_].coverFailed) { coverGenIndex_++; continue; }
        std::string dest = libraryCoverPath(entries_[coverGenIndex_].path);
        if (!Storage.exists(dest.c_str()) || !entries_[coverGenIndex_].coverPath.empty() == false) {
          bool missing = true;
          if (Storage.exists(dest.c_str())) {
            FsFile check;
            if (Storage.openFileForRead("LIB", dest, check)) {
              if (check.size() > 0) { entries_[coverGenIndex_].coverPath = dest; missing = false; }
              check.close();
            }
            if (missing) Storage.remove(dest.c_str());
          }
          if (missing) break;
        }
        coverGenIndex_++;
      }
    }

    if (coverGenIndex_ >= pageStart && coverGenIndex_ < pageEnd) {
      generateCoverForEntry(coverGenIndex_);
      coverGenIndex_++;
    }

    bool allDone = true;
    for (int i = pageStart; i < pageEnd; ++i) {
      if (entries_[i].coverFailed) continue;
      std::string dest = libraryCoverPath(entries_[i].path);
      if (entries_[i].coverPath.empty() || !Storage.exists(dest.c_str())) { allDone = false; break; }
    }
    coversComplete_ = allDone;
    requestUpdate();
    return;
  }

  // Confirm: short press opens book, long press opens context menu
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (total > 0 && selectorIndex_ < total) {
      const unsigned long held = mappedInput.getHeldTime();
      if (held >= 800) {
        const int idx = selectorIndex_;
        const std::string& path = entries_[idx].path;
        const std::string title = entries_[idx].title.empty() ? filenameWithoutExtension(path) : entries_[idx].title;
        const bool isEpub = FsHelpers::hasEpubExtension(path);
        const bool isFav = FAVORITES.isFavorite(path);
        const auto* stats = READING_STATS.findBook(path);
        const bool isCompleted = stats && stats->completed;

        startActivityForResult(
            std::make_unique<BookContextMenuActivity>(renderer, mappedInput, title, isFav, isCompleted, isEpub, true),
            [this, idx, path, isEpub, title](const ActivityResult& result) {
              if (result.isCancelled) { requestUpdate(); return; }
              const auto* menuResult = std::get_if<MenuResult>(&result.data);
              if (!menuResult) { requestUpdate(); return; }
              switch (static_cast<BookContextMenuActivity::MenuAction>(menuResult->action)) {
                case BookContextMenuActivity::MenuAction::OPEN_BOOK:
                  onSelectBook(path); return;
                case BookContextMenuActivity::MenuAction::VIEW_STATS:
                  activityManager.replaceActivity(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, path));
                  return;
                case BookContextMenuActivity::MenuAction::VIEW_METADATA:
                  startActivityForResult(std::make_unique<BookMetadataActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) { requestUpdate(); });
                  return;
                case BookContextMenuActivity::MenuAction::ADD_TO_FAVORITES:
                  FAVORITES.toggleBook(path); requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::MARK_READ_UNREAD: {
                  const auto* s = READING_STATS.findBook(path);
                  const bool wasCompleted = s && s->completed;
                  READING_STATS.beginSession(path, title,
                                             entries_[idx].title.empty() ? "" : entries_[idx].title,
                                             entries_[idx].coverPath.empty() ? "" : entries_[idx].coverPath,
                                             wasCompleted ? 0 : 100);
                  READING_STATS.endSession();
                  requestUpdate();
                  return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_CACHE:
                  if (isEpub) { Epub epub(path, "/.crosspoint"); epub.load(false, true); epub.clearCache(); }
                  requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_COVER_THUMB:
                  deleteLibraryCovers(path); coversComplete_ = false; coverGenIndex_ = -1; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_PAGE_COVER_THUMBS:
                  deletePageCovers(); coversComplete_ = false; coverGenIndex_ = -1; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_ALL_LIBRARY_COVERS:
                  deleteAllLibraryCovers(); coversComplete_ = false; coverGenIndex_ = -1; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::FILTER_ALL_BOOKS:
                  rebuildForFilter(CrossPointSettings::LIBRARY_FILTER_ALL); requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::FILTER_FAVOURITES:
                  rebuildForFilter(CrossPointSettings::LIBRARY_FILTER_FAVOURITES); requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::FILTER_LATEST_READ:
                  rebuildForFilter(CrossPointSettings::LIBRARY_FILTER_LATEST_READ); requestUpdate(); return;
                default:
                  requestUpdate(); return;
              }
            });
        return;
      }
      onSelectBook(entries_[selectorIndex_].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { onGoHome(); return; }
  if (total <= 0) return;

  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectorIndex_ = (selectorIndex_ > 0) ? selectorIndex_ - 1 : total - 1;
    moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectorIndex_ = (selectorIndex_ < total - 1) ? selectorIndex_ + 1 : 0;
    moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    int r = (selectorIndex_ - ps) / gridColumns_;
    int c = selectorIndex_ % gridColumns_;
    if (r == 0) {
      int prev = ps - gridsPerPage_; if (prev < 0) prev = ((total + gridsPerPage_ - 1) / gridsPerPage_ - 1) * gridsPerPage_;
      int items = std::min(gridsPerPage_, total - prev);
      int rows = items / gridColumns_;
      int lc = items - rows * gridColumns_;
      int tc = (c >= lc && lc > 0) ? lc - 1 : c;
      selectorIndex_ = prev + (rows - 1) * gridColumns_ + tc;
    } else { selectorIndex_ -= gridColumns_; }
    moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    int items = std::min(gridsPerPage_, total - ps);
    int rows = items / gridColumns_;
    int r = (selectorIndex_ - ps) / gridColumns_;
    int c = selectorIndex_ % gridColumns_;
    int nr = ps + (r + 1) * gridColumns_ + c;
    if (r >= rows - 1 || nr >= total) {
      int ns = ps + gridsPerPage_; if (ns >= total) ns = 0;
      int ni = ns + c; if (ni >= total) ni = ns;
      selectorIndex_ = ni;
    } else { selectorIndex_ = nr; }
    moved = true;
  }

  if (moved) {
    int curPage = selectorIndex_ / gridsPerPage_;
    if (curPage != lastPage_) {
      coversComplete_ = false;
      coverGenIndex_ = -1;
      lastPage_ = curPage;
    }
    requestUpdate();
  }
}

void LibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int total = static_cast<int>(entries_.size());
  const int totalPages = total > 0 ? (total + gridsPerPage_ - 1) / gridsPerPage_ : 0;
  const int curPage = total > 0 ? selectorIndex_ / gridsPerPage_ + 1 : 0;

  char hdrBuf[32] = {};
  if (total > 0) snprintf(hdrBuf, sizeof(hdrBuf), "%d/%d", curPage, totalPages);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_LIBRARY),
                 total > 0 ? hdrBuf : nullptr);

  // Draw the active filter label centered in the header
  if (total > 0) {
    const char* filterLabel = tr(STR_ALL_BOOKS);
    switch (currentFilter_) {
      case CrossPointSettings::LIBRARY_FILTER_FAVOURITES: filterLabel = tr(STR_FAVOURITES); break;
      case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: filterLabel = tr(STR_LATEST_READ); break;
      default: break;
    }
    if (filterLabel && filterLabel[0]) {
      int lblW = renderer.getTextWidth(UI_10_FONT_ID, filterLabel, EpdFontFamily::REGULAR);
      int centerX = (pageWidth - lblW) / 2;
      int headerY = metrics.topPadding + 8;
      renderer.drawText(UI_10_FONT_ID, centerX, headerY, filterLabel, true, EpdFontFamily::REGULAR);
    }
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
    renderer.displayBuffer();
    return;
  }

  const int pageStart = (curPage - 1) * gridsPerPage_;
  const int pageCount = std::min(gridsPerPage_, total - pageStart);
  const int gap = (gridColumns_ >= 4) ? 8 : 16;
  const int rowPad = (gridColumns_ >= 4) ? 8 : 14;
  const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap;
  const int x0 = (pageWidth - gridW) / 2;
  const int rowH = coverHeight_ + rowPad;

  for (int i = 0; i < pageCount; ++i) {
    const int idx = pageStart + i;
    const int col = i % gridColumns_;
    const int row = i / gridColumns_;
    const int x = x0 + col * (coverWidth_ + gap);
    const int y = contentTop + row * rowH;

    bool drawn = false;
    const auto& cp = entries_[idx].coverPath;
    if (!cp.empty()) {
      if (entries_[idx].coverReady) {
        FsFile file;
        if (Storage.openFileForRead("LIB", cp, file)) {
          Bitmap bmp(file);
          if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
            const float bmpRatio = static_cast<float>(bmp.getWidth()) / static_cast<float>(bmp.getHeight());
            const float tileRatio = static_cast<float>(coverWidth_) / static_cast<float>(coverHeight_);
            const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
            const float cropY = (bmpRatio < tileRatio) ? (1.0f - bmpRatio / tileRatio) : 0.0f;
            renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, Color::White);
            renderer.drawBitmap(bmp, x, y, coverWidth_, coverHeight_, cropX, cropY);
            drawn = true;
          }
          file.close();
        }
        if (!drawn) {
          entries_[idx].coverPath.clear();
          entries_[idx].coverReady = false;
        }
      } else {
        if (!Storage.exists(cp.c_str())) {
          entries_[idx].coverPath.clear();
        } else {
          FsFile file;
          if (Storage.openFileForRead("LIB", cp, file)) {
            Bitmap bmp(file);
            if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
              const float bmpRatio = static_cast<float>(bmp.getWidth()) / static_cast<float>(bmp.getHeight());
              const float tileRatio = static_cast<float>(coverWidth_) / static_cast<float>(coverHeight_);
              const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
              const float cropY = (bmpRatio < tileRatio) ? (1.0f - bmpRatio / tileRatio) : 0.0f;
              renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, Color::White);
              renderer.drawBitmap(bmp, x, y, coverWidth_, coverHeight_, cropX, cropY);
              drawn = true;
              entries_[idx].coverReady = true;
            }
            file.close();
          }
        }
      }
    }

    if (!drawn) {
      renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, Color::White);
      renderer.drawRoundedRect(x, y, coverWidth_, coverHeight_, 1, COVER_CORNER_RADIUS, true);
      std::string t = entries_[idx].title;
      if (t.empty()) t = filenameWithoutExtension(entries_[idx].path);
      constexpr int P = 6;
      auto lines = renderer.wrappedText(SMALL_FONT_ID, t.c_str(), coverWidth_ - 2 * P, 5, EpdFontFamily::BOLD);
      int lh = renderer.getLineHeight(SMALL_FONT_ID);
      int ty = y + (coverHeight_ - static_cast<int>(lines.size()) * lh) / 2;
      for (auto& ln : lines) {
        int tw = renderer.getTextWidth(SMALL_FONT_ID, ln.c_str(), EpdFontFamily::BOLD);
        renderer.drawText(SMALL_FONT_ID, x + (coverWidth_ - tw) / 2, ty, ln.c_str(), true, EpdFontFamily::BOLD);
        ty += lh;
      }
    }

    // Render-time ribbon badge (priority: Read > Favorite > Opened)
    if (drawn) {
      const auto* rbStats = READING_STATS.findBook(entries_[idx].path);
      const bool isComplete = rbStats && rbStats->completed;
      const bool isFav = FAVORITES.isFavorite(entries_[idx].path);
      const bool isOpened = rbStats && rbStats->totalReadingMs > 0 && !isComplete;
      if (isComplete || isFav || isOpened) {
        drawRibbonBadge(renderer, x, y, coverWidth_, coverHeight_, isComplete, isFav, isOpened);
      }
    }

    if (idx == selectorIndex_) {
      renderer.drawRoundedRect(x - 4, y - 4, coverWidth_ + 8, coverHeight_ + 8, 3, COVER_CORNER_RADIUS + 4, true);
      renderer.drawRoundedRect(x - 6, y - 6, coverWidth_ + 12, coverHeight_ + 12, 1, COVER_CORNER_RADIUS + 6, true);
    }
  }

  if (totalPages > 1) {
    constexpr int DS = 8, DSp = 6;
    int dotW = totalPages * DS + (totalPages - 1) * DSp;
    int sx = (pageWidth - dotW) / 2;
    int sy = pageHeight - metrics.buttonHintsHeight - 14 - DS;
    for (int p = 0; p < totalPages; ++p) {
      int dx = sx + p * (DS + DSp);
      if (p == curPage - 1) renderer.fillRect(dx, sy, DS, DS, true);
      else renderer.drawRect(dx, sy, DS, DS, true);
    }
  }

  if (!coversComplete_) {
    int psIdx = (curPage - 1) * gridsPerPage_;
    int pc = std::min(gridsPerPage_, total - psIdx);
    int pe = psIdx + pc;
    int missing = 0;
    for (int i = psIdx; i < pe; ++i) {
      if (entries_[i].coverFailed) continue;
      std::string d = libraryCoverPath(entries_[i].path);
      if (entries_[i].coverPath.empty() || !Storage.exists(d.c_str())) missing++;
    }
    // Draw minimal indexing popup
    if (missing > 0) {
      Rect pr = GUI.drawPopup(renderer, tr(STR_INDEXING));
      if (pr.width > 0 && pr.height > 0) {
        GUI.fillPopupProgress(renderer, pr, (pc - missing) * 100 / std::max(1, pc));
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}

void LibraryActivity::deleteLibraryCovers(const std::string& bookPath) {
  std::string path = libraryCoverPath(bookPath);
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  for (auto& e : entries_) {
    if (e.path == bookPath) { e.coverPath.clear(); e.coverReady = false; e.coverFailed = false; break; }
  }
}

void LibraryActivity::deletePageCovers() {
  int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  int pe = std::min(ps + gridsPerPage_, static_cast<int>(entries_.size()));
  for (int i = ps; i < pe; ++i) {
    std::string p = libraryCoverPath(entries_[i].path);
    if (Storage.exists(p.c_str())) Storage.remove(p.c_str());
    entries_[i].coverPath.clear();
    entries_[i].coverReady = false;
    entries_[i].coverFailed = false;
  }
}

void LibraryActivity::deleteAllLibraryCovers() {
  auto d = Storage.open(libraryCoverDir().c_str());
  if (d && d.isDirectory()) {
    d.rewindDirectory();
    char nb[256];
    for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
      f.getName(nb, sizeof(nb));
      if (!f.isDirectory()) { std::string full = libraryCoverDir() + "/" + nb; f.close(); Storage.remove(full.c_str()); }
      else f.close();
    }
    d.close();
  }
  for (auto& e : entries_) { e.coverPath.clear(); e.coverReady = false; e.coverFailed = false; }
}