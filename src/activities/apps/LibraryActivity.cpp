#include "LibraryActivity.h"

#include <Arduino.h>
#include <Epub.h>
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
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/LibraryCache.h"
#include "components/icons/cover.h"
#include "components/icons/heart.h"
#include "components/LibraryPopupOverlay.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int COVER_CORNER_RADIUS = 2;

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

// Normalize a single char for sort comparison: strip accents then lowercase.
char normalizeChar(unsigned char c) {
  switch (c) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'a';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'e';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'i';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'o';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'u';
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xD1: case 0xF1: return 'n';
    case 0xC7: case 0xE7: return 'c';
    default: break;
  }
  return static_cast<char>(std::tolower(c));
}

std::string normalizeForSort(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    out.push_back(normalizeChar(static_cast<unsigned char>(s[i])));
  }
  return out;
}

std::string filenameWithoutExtension(const std::string& path) {
  std::string name = path;
  const size_t lastSlash = name.find_last_of('/');
  if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);
  const size_t lastDot = name.find_last_of('.');
  if (lastDot != std::string::npos && lastDot > 0) name = name.substr(0, lastDot);
  return name;
}

}  // namespace

void LibraryActivity::applyLayoutFromSettings() {
  switch (SETTINGS.libraryLayout) {
    case CrossPointSettings::LIBRARY_LAYOUT_2X2:
      gridColumns_ = 2; coverWidth_ = 202; coverHeight_ = 306; gap_ = 13; break;
    case CrossPointSettings::LIBRARY_LAYOUT_3X3:
      gridColumns_ = 3; coverWidth_ = 130; coverHeight_ = 190; gap_ = 13; break;
    case CrossPointSettings::LIBRARY_LAYOUT_4X4:
    default:
      gridColumns_ = 4; coverWidth_ = 100; coverHeight_ = 150; gap_ = 7; break;
  }
  gridsPerPage_ = gridColumns_ * gridColumns_;
  rowPad_ = (gridColumns_ >= 4) ? 8 : 14;
}

bool LibraryActivity::isBookCoverReady(const LibraryCache::Entry& entry) const {
  const std::string tp = LibraryCache::thumbPathFor(entry.path, coverWidth_, coverHeight_);
  if (!tp.empty() && Storage.exists(tp.c_str())) return true;
  // Also accept slots we've already generated this pass — the FAT driver
  // may not make the BMP visible immediately after write.
  // Check `coverGeneratedMask_` for entries that were generated in this pass.
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const LibraryCache::Entry& e) { return e.path == entry.path; });
  if (it != entries_.end()) {
    const size_t entryIdx = static_cast<size_t>(it - entries_.begin());
    const size_t pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    if (entryIdx >= pageStart) {
      const size_t slot = entryIdx - pageStart;
      if (slot < 64 && (coverGeneratedMask_ & (uint64_t{1} << slot))) return true;
    }
  }
  return false;
}

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  std::vector<LibraryCache::Entry> allEntries;
  if (!LibraryCache::load(allEntries)) {
    renderer.clearScreen();
    Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
    GUI.fillPopupProgress(renderer, popupRect, 0);
    renderer.displayBuffer();
    LibraryCache::scan(renderer, popupRect, allEntries);
  }

  unfilteredEntries_.clear();
  for (auto& e : allEntries) {
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
    if (include) unfilteredEntries_.push_back(std::move(e));
  }

  currentFilter_ = filter;
  applyFilterAndSort();
}

void LibraryActivity::applyFilterAndSort() {
  std::string searchLower = normalizeForSort(currentSearchText_);

  entries_.clear();
  if (searchLower.empty()) {
    entries_ = unfilteredEntries_;
  } else {
    for (const auto& e : unfilteredEntries_) {
      if (normalizeForSort(e.title).find(searchLower) != std::string::npos) {
        entries_.push_back(e);
        continue;
      }
      if (!e.author.empty() && normalizeForSort(e.author).find(searchLower) != std::string::npos) {
        entries_.push_back(e);
        continue;
      }
      if (normalizeForSort(e.path).find(searchLower) != std::string::npos) {
        entries_.push_back(e);
      }
    }
  }

  auto compareEntries = [this](const LibraryCache::Entry& a, const LibraryCache::Entry& b) -> bool {
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:
        return normalizeForSort(a.title) < normalizeForSort(b.title);
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC:
        return normalizeForSort(a.title) > normalizeForSort(b.title);
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: {
        auto la = normalizeForSort(a.author.empty() ? "zzz" : a.author);
        auto lb = normalizeForSort(b.author.empty() ? "zzz" : b.author);
        if (la != lb) return la < lb;
        return normalizeForSort(a.title) < normalizeForSort(b.title);
      }
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: {
        auto la = normalizeForSort(a.author.empty() ? "zzz" : a.author);
        auto lb = normalizeForSort(b.author.empty() ? "zzz" : b.author);
        if (la != lb) return la > lb;
        return normalizeForSort(a.title) > normalizeForSort(b.title);
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
  lastPage_ = -1;
  forceRender_ = true;
}

void LibraryActivity::scanSd() {
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;

  // Try incremental sync first; falls back to full scan only
  // when the cache file is missing or corrupt.
  if (LibraryCache::sync(unfilteredEntries_)) {
    applyFilterAndSort();
    LOG_DBG("LIB", "Synced %d entries from library cache", static_cast<int>(entries_.size()));
    return;
  }

  // Cache unavailable – full scan with progress popup.
  renderer.clearScreen();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
  GUI.fillPopupProgress(renderer, popupRect, 0);
  renderer.displayBuffer();

  std::vector<LibraryCache::Entry> allEntries;
  LibraryCache::scan(renderer, popupRect, allEntries);

  unfilteredEntries_.clear();
  for (auto& e : allEntries) {
    bool include = false;
    switch (currentFilter_) {
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
    if (include) unfilteredEntries_.push_back(std::move(e));
  }

  applyFilterAndSort();
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
      applyFilterAndSort();
    }
  }
  closePopup();
}

void LibraryActivity::beginTextSearch() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_LIBRARY), currentSearchText_, 30),
      [this](const ActivityResult& result) {
        if (result.isCancelled) { forceRender_ = true; requestUpdate(); return; }
        const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
        if (!kbResult) { forceRender_ = true; requestUpdate(); return; }
        currentSearchText_ = kbResult->text;
        strncpy(SETTINGS.librarySearchText, currentSearchText_.c_str(), sizeof(SETTINGS.librarySearchText) - 1);
        SETTINGS.librarySearchText[sizeof(SETTINGS.librarySearchText) - 1] = '\0';
        SETTINGS.saveToFile();
        applyFilterAndSort();
        forceRender_ = true;
        requestUpdate();
      });
}

// ---- Lifecycle ----

void LibraryActivity::onEnter() {
  Activity::onEnter();
  applyLayoutFromSettings();
  selectorIndex_ = 0; coverGenIndex_ = -1; coversComplete_ = false; lastPage_ = -1;
  lastRenderedPage_ = -1; forceRender_ = true;
  popupMode_ = PopupMode::None;
  upHeld_ = false; upLongTriggered_ = false;
  downHeld_ = false; downLongTriggered_ = false;
  lastLayoutSetting_ = SETTINGS.libraryLayout;
  scanSd();
  requestUpdate();
}

void LibraryActivity::ensureLayoutUpToDate() {
  if (SETTINGS.libraryLayout != lastLayoutSetting_) {
    applyLayoutFromSettings();
    lastLayoutSetting_ = SETTINGS.libraryLayout;
    coversComplete_ = false;
    coverGenIndex_ = -1;
    lastPage_ = -1;
    forceRender_ = true;
  }
}

void LibraryActivity::onExit() {
  Activity::onExit();
  entries_.clear();
  unfilteredEntries_.clear();
}

void LibraryActivity::loop() {
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

  // ---- Detect layout change while activity was in background ----
  ensureLayoutUpToDate();

  // ---- Cover Generation: one pass through the page, retry cycle ----
  if (!coversComplete_ && total > 0) {
    const int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    const int pageCount = std::min(gridsPerPage_, total - pageStart);
    // Reset mask on first entry or page change
    if (coverGenIndex_ < 0) coverGeneratedMask_ = 0;
    // Allow Back to cancel indexing so the user is never stuck.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      coversComplete_ = true;
      coverGenIndex_ = -1;
      coverGeneratedMask_ = 0;
      forceRender_ = true;
      requestUpdate();
      return;
    }
    // Advance cursor past entries that already have covers, then generate
    // the first missing one. When every entry has a cover, the pass is done.
    if (coverGenIndex_ < 0) coverGenIndex_ = 0;
    bool generatedOne = false;
    for (int attempt = 0; attempt < pageCount && !generatedOne; ++attempt) {
      const int slot = (coverGenIndex_ + attempt) % pageCount;
      const int idx = pageStart + slot;
      if (!isBookCoverReady(entries_[idx])) {
        LibraryCache::generateCoverForBook(entries_[idx].path, coverWidth_, coverHeight_);
        if (slot < 64) coverGeneratedMask_ |= (uint64_t{1} << slot);
        generatedOne = true;
      }
    }
    coverGenIndex_++;
    if (coverGenIndex_ >= pageCount) {
      // Finished a full pass. If nothing is missing, mark complete;
      // otherwise restart the pass to retry entries that failed.
      bool stillMissing = false;
      for (int slot = 0; slot < pageCount && !stillMissing; ++slot) {
        if (!isBookCoverReady(entries_[pageStart + slot])) stillMissing = true;
      }
      if (!stillMissing) {
        coversComplete_ = true;
        coverGenIndex_ = -1;
        coverGeneratedMask_ = 0;
      } else {
        coverGenIndex_ = 0;
      }
    }
    forceRender_ = true;
    requestUpdate();
    return;
  }

  if (total <= 0) return;

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
            [this, idx, path, title, isEpub](const ActivityResult& result) {
              if (result.isCancelled) { forceRender_ = true; requestUpdate(); return; }
              const auto* menuResult = std::get_if<MenuResult>(&result.data);
              if (!menuResult) { forceRender_ = true; requestUpdate(); return; }
              switch (static_cast<BookContextMenuActivity::MenuAction>(menuResult->action)) {
                case BookContextMenuActivity::MenuAction::OPEN_BOOK: onSelectBook(path); return;
                case BookContextMenuActivity::MenuAction::VIEW_STATS:
                  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) { forceRender_ = true; requestUpdate(); });
                  return;
                case BookContextMenuActivity::MenuAction::VIEW_METADATA:
                  startActivityForResult(std::make_unique<BookMetadataActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) { forceRender_ = true; requestUpdate(); });
                  return;
                case BookContextMenuActivity::MenuAction::ADD_TO_FAVORITES:
                  FAVORITES.toggleBook(path); forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::MARK_READ_UNREAD: {
                  const auto* s = READING_STATS.findBook(path);
                  const bool wasCompleted = s && s->completed;
                  READING_STATS.beginSession(path, title,
                                            entries_[idx].title.empty() ? "" : entries_[idx].title,
                                            LibraryCache::thumbPathFor(path, coverWidth_, coverHeight_),
                                            wasCompleted ? 0 : 100);
                  READING_STATS.endSession();
                  forceRender_ = true; requestUpdate(); return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_CACHE:
                  if (isEpub) { Epub epub(path, "/.crosspoint"); epub.load(false, true); epub.clearCache(); }
                  forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_COVER_THUMB:
                  deleteLibraryCovers(path);
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_PAGE_COVER_THUMBS:
                  deletePageCovers();
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_ALL_LIBRARY_COVERS:
                  deleteAllLibraryCovers();
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::REINDEX_LIBRARY:
                  LibraryCache::invalidate();
                  scanSd();
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                default: forceRender_ = true; requestUpdate(); return;
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
      renderer.clearScreen();
      renderer.displayBuffer();
      onGoHome();
    }
    return;
  }

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

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld_ && !upLongTriggered_) {
      int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
      int r = (selectorIndex_ - ps) / gridColumns_;
      if (r == 0) {
        int prev = ps - gridsPerPage_;
        if (prev < 0) prev = ((total + gridsPerPage_ - 1) / gridsPerPage_ - 1) * gridsPerPage_;
        int prevItems = std::min(gridsPerPage_, total - prev);
        selectorIndex_ = prev + prevItems - 1;
      } else {
        selectorIndex_ -= gridColumns_;
      }
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) { coversComplete_ = false; coverGenIndex_ = -1; lastPage_ = curPage; }
      requestUpdate();
    }
    upHeld_ = false; upLongTriggered_ = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld_ && !downLongTriggered_) {
      int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
      int pageItems = std::min(gridsPerPage_, total - ps);
      int rows = pageItems / gridColumns_;
      int r = (selectorIndex_ - ps) / gridColumns_;
      int nr = selectorIndex_ + gridColumns_;
      if (r >= rows - 1 || nr >= total || nr >= ps + pageItems) {
        int ns = ps + gridsPerPage_;
        if (ns >= total) ns = 0;
        selectorIndex_ = ns;
      } else {
        selectorIndex_ = nr;
      }
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) { coversComplete_ = false; coverGenIndex_ = -1; lastPage_ = curPage; }
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
    if (curPage != lastPage_) { coversComplete_ = false; coverGenIndex_ = -1; lastPage_ = curPage; }
    requestUpdate();
  }
}

void LibraryActivity::render(RenderLock&&) {
  const int total = static_cast<int>(entries_.size());
  const int curPageRaw = total > 0 ? selectorIndex_ / gridsPerPage_ : 0;
  if (!forceRender_ && popupMode_ == PopupMode::None && coversComplete_ &&
      curPageRaw == lastRenderedPage_ && selectorIndex_ == lastRenderedSelectorIndex_) {
    return;
  }
  forceRender_ = false;
  lastRenderedPage_ = curPageRaw;
  lastRenderedSelectorIndex_ = selectorIndex_;

  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int totalPages = total > 0 ? (total + gridsPerPage_ - 1) / gridsPerPage_ : 0;
  const int curPage = total > 0 ? curPageRaw + 1 : 0;

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
    // Always show sort label
    const char* sortLabel = nullptr;
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:  sortLabel = tr(STR_SORT_TITLE_ASC); break;
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC: sortLabel = tr(STR_SORT_TITLE_DESC); break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: sortLabel = tr(STR_SORT_AUTHOR_ASC); break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: sortLabel = tr(STR_SORT_AUTHOR_DESC); break;
      case CrossPointSettings::LIBRARY_SORT_RECENT:     sortLabel = tr(STR_SORT_RECENT); break;
      case CrossPointSettings::LIBRARY_SORT_PROGRESS:   sortLabel = tr(STR_SORT_PROGRESS); break;
      default: break;
    }
    if (sortLabel && sortLabel[0]) { info += " / "; info += sortLabel; }
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
  const int gap = gap_;
  const int rowPad = rowPad_;
  const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap;
  const int x0 = (pageWidth - gridW) / 2;
  const int rowH = coverHeight_ + rowPad;

  for (int i = 0; i < pageCount; ++i) {
    const int idx = pageStart + i;
    const std::string& thumbPath = LibraryCache::thumbPathFor(entries_[idx].path, coverWidth_, coverHeight_);
    const bool hasThumb = !thumbPath.empty() && Storage.exists(thumbPath.c_str());
    const int col = i % gridColumns_;
    const int row = i / gridColumns_;
    const int x = x0 + col * (coverWidth_ + gap);
    const int y = contentTop + row * rowH;
    bool drawn = false;
    if (hasThumb) {
      FsFile file;
      if (Storage.openFileForRead("LIB", thumbPath, file)) {
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
    }
    if (!drawn) {
      renderer.drawRoundedRect(x, y, coverWidth_, coverHeight_, 1, COVER_CORNER_RADIUS, true);
      renderer.fillRoundedRect(x, y + coverHeight_ / 3, coverWidth_, 2 * coverHeight_ / 3 + 1,
                               COVER_CORNER_RADIUS, false, false, true, true, Color::Black);
      const int iconSize = std::min(32, std::min(coverWidth_ - 4, coverHeight_ / 3 - 4));
      const int iconX = x + (coverWidth_ - iconSize) / 2;
      const int iconY = y + std::max(4, (coverHeight_ / 3 - iconSize) / 2);
      renderer.drawIcon(::CoverIcon, iconX, iconY, iconSize, iconSize);
      std::string t = entries_[idx].title;
      if (t.empty()) t = filenameWithoutExtension(entries_[idx].path);
      constexpr int P = 4;
      const int textAreaH = 2 * coverHeight_ / 3 - 8;
      auto lines = renderer.wrappedText(SMALL_FONT_ID, t.c_str(), coverWidth_ - 2 * P, 3, EpdFontFamily::BOLD);
      int lh = renderer.getLineHeight(SMALL_FONT_ID);
      int ty = y + coverHeight_ / 3 + (textAreaH - static_cast<int>(lines.size()) * lh) / 2;
      for (auto& ln : lines) {
        int tw = renderer.getTextWidth(SMALL_FONT_ID, ln.c_str(), EpdFontFamily::BOLD);
        renderer.drawText(SMALL_FONT_ID, x + (coverWidth_ - tw) / 2, ty, ln.c_str(), false, EpdFontFamily::BOLD);
        ty += lh;
      }
    }
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

  // Show cover generation progress when covers are not yet complete for the current page.
  if (!coversComplete_ && total > 0) {
    const int pageStart = (curPage - 1) * gridsPerPage_;
    const int pageCount = std::min(gridsPerPage_, total - pageStart);
    int readyCount = 0;
    for (int i = 0; i < pageCount; ++i) {
      if (isBookCoverReady(entries_[pageStart + i])) readyCount++;
    }
    Rect pr = GUI.drawPopup(renderer, tr(STR_INDEXING));
    if (pr.width > 0 && pr.height > 0) {
      GUI.fillPopupProgress(renderer, pr, readyCount * 100 / std::max(1, pageCount));
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));

  if (popupMode_ != PopupMode::None) popupOverlay_.render(renderer, pageWidth, pageHeight);
  renderer.displayBuffer();
}

void LibraryActivity::deleteLibraryCovers(const std::string& bookPath) {
  std::string thumbPath = LibraryCache::thumbPathFor(bookPath, coverWidth_, coverHeight_);
  if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
  }
  LibraryCache::removeBook(bookPath);
}

void LibraryActivity::deletePageCovers() {
  int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  int pe = std::min(ps + gridsPerPage_, static_cast<int>(entries_.size()));
  for (int i = ps; i < pe; ++i) {
    std::string thumbPath = LibraryCache::thumbPathFor(entries_[i].path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
    LibraryCache::removeBook(entries_[i].path);
  }
}

void LibraryActivity::deleteAllLibraryCovers() {
  for (auto& e : entries_) {
    std::string thumbPath = LibraryCache::thumbPathFor(e.path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
  }
  for (auto& e : unfilteredEntries_) {
    std::string thumbPath = LibraryCache::thumbPathFor(e.path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
  }
}