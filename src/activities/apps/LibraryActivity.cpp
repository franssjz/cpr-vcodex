#include "LibraryActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "../home/BookContextMenuActivity.h"
#include "../home/BookMetadataActivity.h"
#include "../util/ConfirmationActivity.h"
#include "../util/KeyboardEntryActivity.h"
#include "CrossPointSettings.h"
#include "Epub.h"
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/icons/heart.h"
#include "components/LibraryPopupOverlay.h"
#include "util/CoverRibbonBaker.h"
#include "Txt.h"
#include "Xtc.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int COVER_CORNER_RADIUS = 2;
constexpr const char* LIBRARY_INVENTORY_FILE = "/.crosspoint/library_inventory.json";
constexpr unsigned long INVENTORY_STALE_MS = 30000;

static void fillTopRightTri(GfxRenderer& r, int x, int y, int leg, bool black) {
  for (int dy = 0; dy < leg; ++dy)
    r.fillRect(x + dy, y + dy, leg - dy, 1, black);
}

void drawRibbonBadge(GfxRenderer& r, int cx, int cy, int cw, int ch,
                     bool completed, bool favorite, bool opened) {
  (void)ch;
  const int leg = std::max(20, std::min(cw * 2 / 5, 44));
  const int rx = cx + cw - leg;
  const int ry = cy;

  fillTopRightTri(r, rx - 3, ry - 3, leg + 6, false);
  fillTopRightTri(r, rx - 2, ry - 2, leg + 4, true);
  fillTopRightTri(r, rx - 1, ry - 1, leg + 2, false);
  fillTopRightTri(r, rx,     ry,     leg,     true);

  const int symCx = cx + cw - leg / 3;
  const int symCy = cy + leg / 3;
  const int symSz = std::max(8, leg * 22 / 100);
  (void)symSz;

  if (completed) {
    r.drawLine(symCx - 5, symCy,     symCx - 1, symCy + 4, 2, false);
    r.drawLine(symCx - 1, symCy + 4, symCx + 6, symCy - 4, 2, false);
  } else if (favorite) {
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

bool isExcludedDirectory(const std::string& dirName) {
  if (!dirName.empty() && dirName[0] == '.') return true;
  std::string lower = dirName;
  for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (lower == "crosspoint") return true;
  if (lower == "sleep" || lower.compare(0, 5, "sleep") == 0) return true;
  if (lower == "font" || lower == "fonts") return true;
  if (lower == "dictionaries") return true;
  if (lower == "exports") return true;
  if (lower == "system volume information") return true;
  return false;
}

bool saveInventoryToFile(const std::vector<LibraryEntry>& entries) {
  HalFile file;
  if (!Storage.openFileForWrite("LIB", LIBRARY_INVENTORY_FILE, file)) return false;
  char tsBuf[32];
  snprintf(tsBuf, sizeof(tsBuf), "%lu\n", static_cast<unsigned long>(millis()));
  if (file.write(tsBuf, strlen(tsBuf)) != strlen(tsBuf)) {
    file.close(); Storage.remove(LIBRARY_INVENTORY_FILE); return false;
  }
  for (const auto& e : entries) {
    std::string line = e.path + "|" + e.title + "\n";
    if (file.write(line.c_str(), line.size()) != line.size()) {
      file.close(); Storage.remove(LIBRARY_INVENTORY_FILE); return false;
    }
  }
  file.close();
  return true;
}

bool loadInventoryFromFile(std::vector<LibraryEntry>& entries, unsigned long& outTimestamp) {
  HalFile file;
  if (!Storage.openFileForRead("LIB", LIBRARY_INVENTORY_FILE, file)) return false;
  entries.clear();
  size_t sz = file.size();
  if (sz == 0 || sz > 65536) { file.close(); return false; }
  std::vector<char> buf(sz + 1);
  size_t read = file.read(buf.data(), sz);
  file.close();
  if (read != sz) return false;
  buf[sz] = '\0';

  std::string content(buf.data(), sz);
  size_t nlPos = content.find('\n');
  if (nlPos == std::string::npos) return false;
  std::string tsStr = content.substr(0, nlPos);
  outTimestamp = static_cast<unsigned long>(strtoul(tsStr.c_str(), nullptr, 10));

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
    if (pipePos != std::string::npos) e.title = line.substr(pipePos + 1);
    if (!e.path.empty()) entries.push_back(std::move(e));
  }
  return true;
}

bool isInventoryStale(unsigned long invTimestamp) {
  if (invTimestamp == 0) return true;
  unsigned long now = millis();
  return (now - invTimestamp) > INVENTORY_STALE_MS;
}

bool matchesSearchText(const std::string& text, const std::string& search) {
  if (search.empty()) return true;
  auto it = std::search(
      text.begin(), text.end(),
      search.begin(), search.end(),
      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b)); });
  return it != text.end();
}

}  // namespace

void LibraryActivity::applyLayoutFromSettings() {
  switch (SETTINGS.libraryLayout) {
    case CrossPointSettings::LIBRARY_LAYOUT_2X2:
      gridColumns_ = 2; coverWidth_ = 202; coverHeight_ = 310; break;
    case CrossPointSettings::LIBRARY_LAYOUT_3X3:
      gridColumns_ = 3; coverWidth_ = 130; coverHeight_ = 190; break;
    case CrossPointSettings::LIBRARY_LAYOUT_4X4:
    default:
      gridColumns_ = 4; coverWidth_ = 100; coverHeight_ = 150; break;
  }
  gridsPerPage_ = gridColumns_ * gridColumns_;
}

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  std::vector<LibraryEntry> newEntries;

  std::function<void(const std::string&)> walk;
  walk = [&](const std::string& dir) {
    auto d = Storage.open(dir.c_str());
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    d.rewindDirectory();
    char nb[256];
    for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
      f.getName(nb, sizeof(nb));
      if (f.isDirectory()) {
        std::string dirName = nb;
        if (isExcludedDirectory(dirName)) { f.close(); continue; }
        f.close();
        walk(dir + dirName + '/');
      } else if (isEbookExtension(nb)) {
        if (strcmp(nb, "if_found.txt") == 0 || strcmp(nb, "crash_report.txt") == 0) { f.close(); continue; }
        LibraryEntry e;
        e.path = dir + nb;
        e.title = filenameWithoutExtension(e.path);
        newEntries.push_back(std::move(e));
      }
      f.close();
    }
    d.close();
  };

  const char* rootDir = SETTINGS.libraryRootDir;
  if (rootDir[0] == '\0' || (rootDir[0] == '/' && rootDir[1] == '\0')) {
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
          newEntries.push_back(std::move(e));
        }
        f.close();
      }
      rd.close();
    }
  } else {
    std::string root(rootDir);
    if (root.empty() || root.back() != '/') root += '/';
    walk(root);
  }

  saveInventoryToFile(newEntries);

  entries_.clear();
  for (auto& e : newEntries) {
    bool include = false;
    switch (filter) {
      case CrossPointSettings::LIBRARY_FILTER_ALL: include = true; break;
      case CrossPointSettings::LIBRARY_FILTER_FAVOURITES: include = FAVORITES.isFavorite(e.path); break;
      case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: {
        const auto& recent = RECENT_BOOKS.getBooks();
        for (const auto& rb : recent) {
          if (rb.path == e.path || (!rb.bookId.empty() && rb.bookId == e.path)) { include = true; break; }
        }
        break;
      }
    }
    if (include) entries_.push_back(std::move(e));
  }

  currentFilter_ = filter;
  applyFilterAndSort();
  inventoryLoaded_ = true;
}

void LibraryActivity::applyFilterAndSort() {
  auto compareEntries = [this](const LibraryEntry& a, const LibraryEntry& b) -> bool {
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC: {
        auto la = a.title, lb = b.title;
        for (auto& c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return la < lb;
      }
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC: {
        auto la = a.title, lb = b.title;
        for (auto& c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return la > lb;
      }
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: {
        auto la = a.author.empty() ? "zzz" : a.author;
        auto lb = b.author.empty() ? "zzz" : b.author;
        for (auto& c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (la != lb) return la < lb;
        auto ta = a.title, tb = b.title;
        for (auto& c : ta) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : tb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ta < tb;
      }
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: {
        auto la = a.author.empty() ? "zzz" : a.author;
        auto lb = b.author.empty() ? "zzz" : b.author;
        for (auto& c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (la != lb) return la > lb;
        auto ta = a.title, tb = b.title;
        for (auto& c : ta) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : tb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ta > tb;
      }
      case CrossPointSettings::LIBRARY_SORT_RECENT: {
        const auto* sa = READING_STATS.findBook(a.path);
        const auto* sb = READING_STATS.findBook(b.path);
        uint32_t ta = sa ? sa->lastReadAt : 0;
        uint32_t tb = sb ? sb->lastReadAt : 0;
        if (ta != tb) return ta > tb;
        return a.title < b.title;
      }
      case CrossPointSettings::LIBRARY_SORT_PROGRESS: {
        const auto* sa = READING_STATS.findBook(a.path);
        const auto* sb = READING_STATS.findBook(b.path);
        uint8_t pa = sa ? sa->lastProgressPercent : 0;
        uint8_t pb = sb ? sb->lastProgressPercent : 0;
        bool ca = sa && sa->completed;
        bool cb = sb && sb->completed;
        if (ca != cb) return ca;
        if (pa != pb) return pa > pb;
        return a.title < b.title;
      }
    }
    return a.path < b.path;
  };
  std::sort(entries_.begin(), entries_.end(), compareEntries);
  selectorIndex_ = 0;
  coversComplete_ = false;
  coverGenIndex_ = -1;
}

void LibraryActivity::scanSd() {
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;
  inventoryLoaded_ = false;
  if (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL) {
    unsigned long invTimestamp = 0;
    std::vector<LibraryEntry> cachedEntries;
    if (loadInventoryFromFile(cachedEntries, invTimestamp) && !isInventoryStale(invTimestamp)) {
      inventoryLoaded_ = true;
      entries_ = std::move(cachedEntries);
      applyFilterAndSort();
      LOG_DBG("LIB", "Loaded %d entries from inventory cache", static_cast<int>(entries_.size()));
      return;
    }
  }
  rebuildForFilter(currentFilter_);
}

// Generate cover thumb directly into parser cache — same approach as carousel.
// Uses load(false,true) first (fast, existing cache), falls back to load(true,true).
// Returns the resolved BMP path, or empty string on failure.
std::string LibraryActivity::generateOneCover(const std::string& bookPath, int coverW, int coverH) {
  std::string fname = bookPath;
  size_t slash = fname.find_last_of('/');
  if (slash != std::string::npos) fname = fname.substr(slash + 1);

  if (FsHelpers::hasEpubExtension(fname)) {
    Epub epub(bookPath, "/.crosspoint");
    // Fast path: try existing cache first (books already opened have it)
    if (!epub.load(false, true) && !epub.load(true, true)) return {};
    if (!epub.generateThumbBmp(coverW, coverH)) return {};
    std::string path = epub.getThumbBmpPath(coverW, coverH);
    if (path.empty() || !Storage.exists(path.c_str())) return {};
    return path;
  } else if (FsHelpers::hasXtcExtension(fname)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) return {};
    if (!xtc.generateThumbBmp(coverW, coverH)) return {};
    std::string path = xtc.getThumbBmpPath(coverW, coverH);
    if (path.empty() || !Storage.exists(path.c_str())) return {};
    return path;
  } else if (FsHelpers::hasTxtExtension(fname) || FsHelpers::hasMarkdownExtension(fname)) {
    Txt txt(bookPath, "/.crosspoint");
    if (!txt.load()) return {};
    if (!txt.generateCoverBmp()) return {};
    std::string path = txt.getCoverBmpPath();
    if (path.empty() || !Storage.exists(path.c_str())) return {};
    return path;
  }
  return {};
}

// Generate all missing covers for the current page in one batch.
// Called from loop() only once per page.
void LibraryActivity::generatePageCovers() {
  int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  int pageCount = std::min(gridsPerPage_, static_cast<int>(entries_.size()) - pageStart);
  for (int i = 0; i < pageCount; ++i) {
    int idx = pageStart + i;
    LibraryEntry& e = entries_[idx];
    if (e.coverFailed || !e.coverPath.empty()) continue;
    std::string path = generateOneCover(e.path, coverWidth_, coverHeight_);
    if (!path.empty()) {
      e.coverPath = path;
      CoverRibbonBaker::bakeIntoFile(path, e.path);
    } else {
      e.coverFailed = true;
    }
  }
  coversComplete_ = true;
}

// ---- Popup Methods ----

void LibraryActivity::openSortPopup() {
  popupMode_ = PopupMode::Sort;
  popupOverlay_.title = I18N.get(StrId::STR_LIBRARY_SORT);
  popupOverlay_.items.clear();
  popupOverlay_.selectedIndex = 0;
  popupOverlay_.startIndex = 0;

  struct { StrId id; CrossPointSettings::LIBRARY_SORT sort; } sorts[] = {
    {StrId::STR_SORT_TITLE_ASC, CrossPointSettings::LIBRARY_SORT_TITLE_ASC},
    {StrId::STR_SORT_TITLE_DESC, CrossPointSettings::LIBRARY_SORT_TITLE_DESC},
    {StrId::STR_SORT_AUTHOR_ASC, CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC},
    {StrId::STR_SORT_AUTHOR_DESC, CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC},
    {StrId::STR_SORT_RECENT, CrossPointSettings::LIBRARY_SORT_RECENT},
    {StrId::STR_SORT_PROGRESS, CrossPointSettings::LIBRARY_SORT_PROGRESS},
  };
  for (int i = 0; i < 6; ++i) {
    PopupItem item;
    item.label = I18N.get(sorts[i].id);
    item.selected = (currentSort_ == sorts[i].sort);
    popupOverlay_.items.push_back(item);
    if (item.selected) {
      popupOverlay_.selectedIndex = i;
      popupOverlay_.startIndex = std::max(0, i - LibraryPopupOverlay::kMaxVisibleRows / 2);
    }
  }
  requestUpdate();
}

void LibraryActivity::openFilterPopup() {
  popupMode_ = PopupMode::Filter;
  popupOverlay_.title = I18N.get(StrId::STR_LIBRARY_FILTER);
  popupOverlay_.items.clear();
  popupOverlay_.selectedIndex = 0;
  popupOverlay_.startIndex = 0;

  PopupItem allItem; allItem.label = I18N.get(StrId::STR_ALL_BOOKS);
  allItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL);
  popupOverlay_.items.push_back(allItem);

  PopupItem favItem; favItem.label = I18N.get(StrId::STR_FAVOURITES);
  favItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_FAVOURITES);
  popupOverlay_.items.push_back(favItem);

  PopupItem recentItem; recentItem.label = I18N.get(StrId::STR_LATEST_READ);
  recentItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_LATEST_READ);
  popupOverlay_.items.push_back(recentItem);

  PopupItem searchItem; searchItem.label = I18N.get(StrId::STR_SEARCH_LIBRARY);
  searchItem.selected = false;
  popupOverlay_.items.push_back(searchItem);

  PopupItem clearItem; clearItem.label = I18N.get(StrId::STR_SEARCH_CLEAR);
  clearItem.selected = false;
  popupOverlay_.items.push_back(clearItem);

  requestUpdate();
}

void LibraryActivity::closePopup() {
  popupMode_ = PopupMode::None;
  requestUpdate();
}

void LibraryActivity::selectPopupItem() {
  if (popupMode_ == PopupMode::None) return;
  int idx = popupOverlay_.selectedIndex;
  if (idx < 0 || idx >= static_cast<int>(popupOverlay_.items.size())) return;

  if (popupMode_ == PopupMode::Sort) {
    CrossPointSettings::LIBRARY_SORT sorts[] = {
      CrossPointSettings::LIBRARY_SORT_TITLE_ASC, CrossPointSettings::LIBRARY_SORT_TITLE_DESC,
      CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC, CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC,
      CrossPointSettings::LIBRARY_SORT_RECENT, CrossPointSettings::LIBRARY_SORT_PROGRESS,
    };
    if (idx < 6) {
      currentSort_ = sorts[idx];
      SETTINGS.librarySort = currentSort_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    }
  } else if (popupMode_ == PopupMode::Filter) {
    if (idx == 0) {
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 1) {
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_FAVOURITES;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 2) {
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_LATEST_READ;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 3) {
      closePopup();
      beginTextSearch();
      return;
    } else if (idx == 4) {
      currentSearchText_.clear();
      SETTINGS.librarySearchText[0] = '\0';
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    }
  }
  closePopup();
}

void LibraryActivity::beginTextSearch() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_LIBRARY), currentSearchText_, 30),
      [this](const ActivityResult& result) {
        if (result.isCancelled) { requestUpdate(); return; }
        const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
        if (!kbResult) { requestUpdate(); return; }
        currentSearchText_ = kbResult->text;
        strncpy(SETTINGS.librarySearchText, currentSearchText_.c_str(), sizeof(SETTINGS.librarySearchText) - 1);
        SETTINGS.librarySearchText[sizeof(SETTINGS.librarySearchText) - 1] = '\0';
        SETTINGS.saveToFile();
        applyFilterAndSort();
        requestUpdate();
      });
}

// ---- Lifecycle ----

void LibraryActivity::onEnter() {
  Activity::onEnter();
  applyLayoutFromSettings();
  selectorIndex_ = 0; coverGenIndex_ = -1; coversComplete_ = false; lastPage_ = -1;
  popupMode_ = PopupMode::None;
  upHeld_ = false; upLongTriggered_ = false;
  downHeld_ = false; downLongTriggered_ = false;
  scanSd();
  requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  entries_.clear();
}

void LibraryActivity::loop() {
  // ---- Popup Mode Navigation ----
  if (popupMode_ != PopupMode::None) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { closePopup(); return; }
    int itemCount = static_cast<int>(popupOverlay_.items.size());
    int& sel = popupOverlay_.selectedIndex;
    int& start = popupOverlay_.startIndex;
    int visible = std::min(itemCount, LibraryPopupOverlay::kMaxVisibleRows);
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (sel > 0) { sel--; if (sel < start) start = sel; }
      else { sel = itemCount - 1; start = std::max(0, itemCount - visible); }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (sel < itemCount - 1) { sel++; if (sel >= start + visible) start = sel - visible + 1; }
      else { sel = 0; start = 0; }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { selectPopupItem(); return; }
    return;
  }

  const int total = static_cast<int>(entries_.size());

  // ---- Cover Generation: batch-generate all page covers on first entry ----
  if (!coversComplete_ && total > 0) {
    generatePageCovers();
    requestUpdate();
    return;
  }

  // ---- Confirm: short press opens book, long press opens context menu ----
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
                case BookContextMenuActivity::MenuAction::OPEN_BOOK: onSelectBook(path); return;
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
                  requestUpdate(); return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_CACHE:
                  if (isEpub) { Epub epub(path, "/.crosspoint"); epub.load(false, true); epub.clearCache(); }
                  requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_COVER_THUMB:
                  deleteLibraryCovers(path); coversComplete_ = false; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_PAGE_COVER_THUMBS:
                  deletePageCovers(); coversComplete_ = false; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_ALL_LIBRARY_COVERS:
                  deleteAllLibraryCovers(); coversComplete_ = false; requestUpdate(); return;
                default: requestUpdate(); return;
              }
            });
        return;
      }
      onSelectBook(entries_[selectorIndex_].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (upHeld_ || downHeld_) {
      upHeld_ = false; downHeld_ = false;
      upLongTriggered_ = false; downLongTriggered_ = false;
    } else {
      onGoHome();
    }
    return;
  }
  if (total <= 0) return;

  // ---- Long-press detection for Up (Sort) and Down (Filter) ----
  if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
    if (!upHeld_) { upHeld_ = true; upLongTriggered_ = false; }
    if (!upLongTriggered_ && mappedInput.getHeldTime() >= kLongPressMs) {
      upLongTriggered_ = true;
      openSortPopup();
      return;
    }
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Down)) {
    if (!downHeld_) { downHeld_ = true; downLongTriggered_ = false; }
    if (!downLongTriggered_ && mappedInput.getHeldTime() >= kLongPressMs) {
      downLongTriggered_ = true;
      openFilterPopup();
      return;
    }
  }

  // ---- Up short release = navigate up ----
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld_ && !upLongTriggered_) {
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
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) { coversComplete_ = false; lastPage_ = curPage; }
      requestUpdate();
    }
    upHeld_ = false; upLongTriggered_ = false;
  }

  // ---- Down short release = navigate down ----
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld_ && !downLongTriggered_) {
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
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) { coversComplete_ = false; lastPage_ = curPage; }
      requestUpdate();
    }
    downHeld_ = false; downLongTriggered_ = false;
  }

  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectorIndex_ = (selectorIndex_ > 0) ? selectorIndex_ - 1 : total - 1; moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectorIndex_ = (selectorIndex_ < total - 1) ? selectorIndex_ + 1 : 0; moved = true;
  }
  if (moved) {
    int curPage = selectorIndex_ / gridsPerPage_;
    if (curPage != lastPage_) { coversComplete_ = false; lastPage_ = curPage; }
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

  if (total > 0) {
    std::string info;
    switch (currentFilter_) {
      case CrossPointSettings::LIBRARY_FILTER_FAVOURITES: info = tr(STR_FAVOURITES); break;
      case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: info = tr(STR_LATEST_READ); break;
      default: info = tr(STR_ALL_BOOKS); break;
    }
    const char* sortLabel = nullptr;
    if (currentSort_ != CrossPointSettings::LIBRARY_SORT_TITLE_ASC) {
      switch (currentSort_) {
        case CrossPointSettings::LIBRARY_SORT_TITLE_DESC: sortLabel = tr(STR_SORT_TITLE_DESC); break;
        case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: sortLabel = tr(STR_SORT_AUTHOR_ASC); break;
        case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: sortLabel = tr(STR_SORT_AUTHOR_DESC); break;
        case CrossPointSettings::LIBRARY_SORT_RECENT:     sortLabel = tr(STR_SORT_RECENT); break;
        case CrossPointSettings::LIBRARY_SORT_PROGRESS:   sortLabel = tr(STR_SORT_PROGRESS); break;
        default: break;
      }
      if (sortLabel && sortLabel[0]) { info += " / "; info += sortLabel; }
    }
    if (!currentSearchText_.empty()) {
      info += " [";
      info += currentSearchText_.size() > 20 ? currentSearchText_.substr(0, 20) + ".." : currentSearchText_;
      info += "]";
    }
    int lblW = renderer.getTextWidth(UI_10_FONT_ID, info.c_str(), EpdFontFamily::REGULAR);
    if (lblW > pageWidth - 20) {
      while (info.size() > 5 && renderer.getTextWidth(UI_10_FONT_ID, (info + "..").c_str(), EpdFontFamily::REGULAR) > pageWidth - 20) {
        info.pop_back();
      }
      info += "..";
    }
    lblW = renderer.getTextWidth(UI_10_FONT_ID, info.c_str(), EpdFontFamily::REGULAR);
    int centerX = (pageWidth - lblW) / 2;
    int headerY = metrics.topPadding + 8;
    renderer.drawText(UI_10_FONT_ID, centerX, headerY, info.c_str(), true, EpdFontFamily::REGULAR);
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));
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

  int missingCovers = 0;
  for (int i = 0; i < pageCount; ++i) {
    const int idx = pageStart + i;
    const int col = i % gridColumns_;
    const int row = i / gridColumns_;
    const int x = x0 + col * (coverWidth_ + gap);
    const int y = contentTop + row * rowH;
    bool drawn = false;
    bool isFailed = entries_[idx].coverFailed;
    const auto& cp = entries_[idx].coverPath;
    if (!isFailed && !cp.empty()) {
      if (!entries_[idx].coverReady && !Storage.exists(cp.c_str())) {
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
        if (!drawn) { entries_[idx].coverPath.clear(); entries_[idx].coverReady = false; }
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
    if (!drawn && !isFailed) missingCovers++;
    if (drawn) {
      const auto* rbStats = READING_STATS.findBook(entries_[idx].path);
      const bool isComplete = rbStats && rbStats->completed;
      const bool isFav = FAVORITES.isFavorite(entries_[idx].path);
      const bool isOpened = rbStats && rbStats->totalReadingMs > 0 && !isComplete;
      if (isComplete || isFav || isOpened) drawRibbonBadge(renderer, x, y, coverWidth_, coverHeight_, isComplete, isFav, isOpened);
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

  if (!coversComplete_ && missingCovers > 0) {
    Rect pr = GUI.drawPopup(renderer, tr(STR_INDEXING));
    if (pr.width > 0 && pr.height > 0) GUI.fillPopupProgress(renderer, pr, (pageCount - missingCovers) * 100 / std::max(1, pageCount));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));

  if (popupMode_ != PopupMode::None) popupOverlay_.render(renderer, pageWidth, pageHeight);
  renderer.displayBuffer();
}

void LibraryActivity::deleteLibraryCovers(const std::string& bookPath) {
  for (auto& e : entries_) {
    if (e.path == bookPath) {
      if (!e.coverPath.empty() && Storage.exists(e.coverPath.c_str())) {
        Storage.remove(e.coverPath.c_str());
      }
      e.coverPath.clear(); e.coverReady = false; e.coverFailed = false;
      break;
    }
  }
}

void LibraryActivity::deletePageCovers() {
  int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  int pe = std::min(ps + gridsPerPage_, static_cast<int>(entries_.size()));
  for (int i = ps; i < pe; ++i) {
    if (!entries_[i].coverPath.empty() && Storage.exists(entries_[i].coverPath.c_str())) {
      Storage.remove(entries_[i].coverPath.c_str());
    }
    entries_[i].coverPath.clear(); entries_[i].coverReady = false; entries_[i].coverFailed = false;
  }
}

void LibraryActivity::deleteAllLibraryCovers() {
  for (auto& e : entries_) {
    if (!e.coverPath.empty() && Storage.exists(e.coverPath.c_str())) {
      Storage.remove(e.coverPath.c_str());
    }
    e.coverPath.clear(); e.coverReady = false; e.coverFailed = false;
  }
}