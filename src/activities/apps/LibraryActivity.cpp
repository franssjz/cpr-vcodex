#include "LibraryActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "Epub.h"
#include "MappedInputManager.h"
#include "Txt.h"
#include "Xtc.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int COVER_CORNER_RADIUS = 2;

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

void cleanupTempEpubThumbs(const std::string& bookPath, int coverH) {
  if (!FsHelpers::hasEpubExtension(bookPath)) return;
  // EPUB thumb generation writes a temporary BMP at a hash-derived path.
  // After copying to our library_covers dir, remove the temp source.
  Epub epub(bookPath, "/.crosspoint");
  std::string src = UITheme::getCoverThumbPath(epub.getThumbBmpPath(), coverH);
  if (!src.empty() && Storage.exists(src.c_str())) {
    FsFile f;
    if (Storage.openFileForRead("LIB", src, f)) {
      size_t sz = f.size();
      f.close();
      if (sz == 0) {
        Storage.remove(src.c_str());
        LOG_DBG("LIB", "Removed zero-size temp thumb: %s", src.c_str());
      }
    }
  }
}

bool generateOneCover(const std::string& bookPath, int coverH, const std::string& destFile) {
  std::string fname = bookPath;
  size_t slash = fname.find_last_of('/');
  if (slash != std::string::npos) fname = fname.substr(slash + 1);

  if (FsHelpers::hasEpubExtension(fname)) {
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(true, true)) return false;
    if (!epub.generateThumbBmp(coverH)) return false;
    std::string src = UITheme::getCoverThumbPath(epub.getThumbBmpPath(), coverH);
    if (src.empty() || !Storage.exists(src.c_str())) return false;
    FsFile fin;
    if (!Storage.openFileForRead("LIB", src, fin)) return false;
    size_t sz = fin.size();
    if (sz == 0) {
      fin.close();
      Storage.remove(src.c_str());
      return false;
    }
    std::vector<uint8_t> buf(sz);
    fin.read(buf.data(), sz);
    fin.close();
    FsFile fout;
    if (!Storage.openFileForWrite("LIB", destFile, fout)) return false;
    size_t written = fout.write(buf.data(), sz);
    fout.close();
    if (written != sz) {
      Storage.remove(destFile.c_str());
      return false;
    }
    // Clean up the temp thumb so it doesn't accumulate
    Storage.remove(src.c_str());
    return true;
  }

  if (FsHelpers::hasXtcExtension(fname)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) return false;
    if (!xtc.generateThumbBmp(coverH)) return false;
    std::string src = UITheme::getCoverThumbPath(xtc.getThumbBmpPath(), coverH);
    if (src.empty() || !Storage.exists(src.c_str())) return false;
    FsFile fin;
    if (!Storage.openFileForRead("LIB", src, fin)) return false;
    size_t sz = fin.size();
    if (sz == 0) {
      fin.close();
      Storage.remove(src.c_str());
      return false;
    }
    std::vector<uint8_t> buf(sz);
    fin.read(buf.data(), sz);
    fin.close();
    FsFile fout;
    if (!Storage.openFileForWrite("LIB", destFile, fout)) return false;
    size_t written = fout.write(buf.data(), sz);
    fout.close();
    if (written != sz) {
      Storage.remove(destFile.c_str());
      return false;
    }
    Storage.remove(src.c_str());
    return true;
  }

  if (FsHelpers::hasTxtExtension(fname) || FsHelpers::hasMarkdownExtension(fname)) {
    Txt txt(bookPath, "/.crosspoint");
    if (!txt.load()) return false;
    if (!txt.generateCoverBmp()) return false;
    std::string src = txt.getCoverBmpPath();
    if (src.empty() || !Storage.exists(src.c_str())) return false;
    FsFile fin;
    if (!Storage.openFileForRead("LIB", src, fin)) return false;
    size_t sz = fin.size();
    if (sz == 0) {
      fin.close();
      Storage.remove(src.c_str());
      return false;
    }
    std::vector<uint8_t> buf(sz);
    fin.read(buf.data(), sz);
    fin.close();
    FsFile fout;
    if (!Storage.openFileForWrite("LIB", destFile, fout)) return false;
    size_t written = fout.write(buf.data(), sz);
    fout.close();
    if (written != sz) {
      Storage.remove(destFile.c_str());
      return false;
    }
    Storage.remove(src.c_str());
    return true;
  }

  return false;
}
}  // namespace

std::string LibraryActivity::libraryCoverPath(const std::string& bookPath) const {
  return libraryCoverPathFor(bookPath, coverWidth_, coverHeight_);
}

void LibraryActivity::applyLayoutFromSettings() {
  switch (SETTINGS.libraryLayout) {
    case CrossPointSettings::LIBRARY_LAYOUT_2X2:
      gridColumns_ = 2; coverWidth_ = 220; coverHeight_ = 320; break;
    case CrossPointSettings::LIBRARY_LAYOUT_3X3:
      gridColumns_ = 3; coverWidth_ = 130; coverHeight_ = 190; break;
    case CrossPointSettings::LIBRARY_LAYOUT_4X4:
    default:
      gridColumns_ = 4; coverWidth_ = 100; coverHeight_ = 150; break;
  }
  gridsPerPage_ = gridColumns_ * gridColumns_;
}

void LibraryActivity::scanSd() {
  entries_.clear();
  Storage.mkdir(libraryCoverDir().c_str());

  std::function<void(const std::string&)> walk;
  walk = [&](const std::string& dir) {
    auto d = Storage.open(dir.c_str());
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    d.rewindDirectory();
    char nb[256];
    for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
      f.getName(nb, sizeof(nb));
      if (nb[0] == '.' && strcmp(nb, "System Volume Information") != 0) { f.close(); continue; }
      std::string entry = dir + nb;
      if (f.isDirectory()) { f.close(); walk(entry + '/'); }
      else if (isEbookExtension(nb)) {
        LibraryEntry e;
        e.path = entry;
        e.title = filenameWithoutExtension(entry);
        std::string cv = libraryCoverPath(entry);
        if (Storage.exists(cv.c_str())) e.coverPath = cv;
        entries_.push_back(std::move(e));
      }
      f.close();
    }
    d.close();
  };

  walk("/");
  std::sort(entries_.begin(), entries_.end(), [](auto& a, auto& b) { return a.path < b.path; });
}

void LibraryActivity::generateCoverForEntry(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  LibraryEntry& e = entries_[index];
  std::string dest = libraryCoverPath(e.path);
  // Check if existing thumb is valid (non-zero size)
  if (Storage.exists(dest.c_str())) {
    FsFile check;
    if (Storage.openFileForRead("LIB", dest, check)) {
      if (check.size() > 0) {
        check.close();
        e.coverPath = dest;
        return;
      }
      check.close();
    }
    // Zero-size or unreadable — delete and regenerate
    Storage.remove(dest.c_str());
  }
  if (generateOneCover(e.path, coverHeight_, dest)) e.coverPath = dest;
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  applyLayoutFromSettings();
  selectorIndex_ = 0;
  coverGenIndex_ = -1;
  coversComplete_ = false;
  Storage.mkdir(libraryCoverDir().c_str());
  cleanupZeroSizeThumbs();
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

    // Find first missing cover on this page
    if (coverGenIndex_ < pageStart || coverGenIndex_ >= pageEnd) {
      coverGenIndex_ = pageStart;
      // Skip already-existing covers
      while (coverGenIndex_ < pageEnd) {
        std::string dest = libraryCoverPath(entries_[coverGenIndex_].path);
        if (!Storage.exists(dest.c_str()) || !entries_[coverGenIndex_].coverPath.empty() == false) {
          // Need to check if truly missing
          bool missing = true;
          if (Storage.exists(dest.c_str())) {
            FsFile check;
            if (Storage.openFileForRead("LIB", dest, check)) {
              if (check.size() > 0) {
                entries_[coverGenIndex_].coverPath = dest;
                missing = false;
              }
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

    // Check if all covers on this page are done
    bool allDone = true;
    for (int i = pageStart; i < pageEnd; ++i) {
      std::string dest = libraryCoverPath(entries_[i].path);
      if (entries_[i].coverPath.empty() || !Storage.exists(dest.c_str())) {
        allDone = false;
        break;
      }
    }
    coversComplete_ = allDone;
    requestUpdate();
    return;
  }

  // Confirm opens selected book
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (total > 0 && selectorIndex_ < total) {
      onSelectBook(entries_[selectorIndex_].path);
      return;
    }
  }

  // Back returns to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (total <= 0) return;

  bool moved = false;

  // LEFT: previous item, wraps to last if at start
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectorIndex_ = (selectorIndex_ > 0) ? selectorIndex_ - 1 : total - 1;
    moved = true;
  }

  // RIGHT: next item, wraps to first if at end
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectorIndex_ = (selectorIndex_ < total - 1) ? selectorIndex_ + 1 : 0;
    moved = true;
  }

  // UP: go up one row; if at top row, go to previous page's bottom row
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    int pageStartIdx = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    int rowInPage = (selectorIndex_ - pageStartIdx) / gridColumns_;
    int col = selectorIndex_ % gridColumns_;
    if (rowInPage == 0) {
      int prevStart = pageStartIdx - gridsPerPage_;
      if (prevStart < 0) prevStart = ((total + gridsPerPage_ - 1) / gridsPerPage_ - 1) * gridsPerPage_;
      int itemsOnPrev = std::min(gridsPerPage_, total - prevStart);
      int rows = itemsOnPrev / gridColumns_;
      int lastColItems = itemsOnPrev - rows * gridColumns_;
      int tc = col;
      if (tc >= lastColItems && lastColItems > 0) tc = lastColItems - 1;
      selectorIndex_ = prevStart + (rows - 1) * gridColumns_ + tc;
    } else {
      selectorIndex_ -= gridColumns_;
    }
    moved = true;
  }

  // DOWN: go down one row; if at bottom row, go to next page's top row
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    int pageStartIdx = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    int itemsOnPage = std::min(gridsPerPage_, total - pageStartIdx);
    int rowsOnPage = itemsOnPage / gridColumns_;
    int rowInPage = (selectorIndex_ - pageStartIdx) / gridColumns_;
    int col = selectorIndex_ % gridColumns_;
    int nextItemRow0 = pageStartIdx + (rowInPage + 1) * gridColumns_ + col;
    if (rowInPage >= rowsOnPage - 1 || nextItemRow0 >= total) {
      int nextStart = pageStartIdx + gridsPerPage_;
      if (nextStart >= total) nextStart = 0;
      int idx = nextStart + col;
      if (idx >= total) idx = nextStart;
      selectorIndex_ = idx;
    } else {
      selectorIndex_ = nextItemRow0;
    }
    moved = true;
  }

  if (moved) {
    coversComplete_ = false;
    coverGenIndex_ = -1;
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

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
    renderer.displayBuffer();
    return;
  }

  // Always draw the grid first
  const int pageStart = (curPage - 1) * gridsPerPage_;
  const int pageCount = std::min(gridsPerPage_, total - pageStart);
  const int gap = 16;
  const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap;
  const int x0 = (pageWidth - gridW) / 2;
  const int rowH = coverHeight_ + 14;

  for (int i = 0; i < pageCount; ++i) {
    const int idx = pageStart + i;
    const int col = i % gridColumns_;
    const int row = i / gridColumns_;
    const int x = x0 + col * (coverWidth_ + gap);
    const int y = contentTop + row * rowH;

    bool drawn = false;
    const auto& cp = entries_[idx].coverPath;
    if (!cp.empty() && Storage.exists(cp.c_str())) {
      FsFile file;
      if (Storage.openFileForRead("LIB", cp, file)) {
        Bitmap bmp(file);
        if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
          renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, Color::White);
          renderer.drawBitmap(bmp, x, y, coverWidth_, coverHeight_, 0, 0);
          drawn = true;
        }
        file.close();
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

    if (idx == selectorIndex_) {
      renderer.drawRoundedRect(x - 4, y - 4, coverWidth_ + 8, coverHeight_ + 8, 3, COVER_CORNER_RADIUS + 4, true);
      renderer.drawRoundedRect(x - 6, y - 6, coverWidth_ + 12, coverHeight_ + 12, 1, COVER_CORNER_RADIUS + 6, true);
    }
  }

  // Page dots
  if (totalPages > 1) {
    constexpr int DS = 8;
    constexpr int DSp = 6;
    int dotW = totalPages * DS + (totalPages - 1) * DSp;
    int sx = (pageWidth - dotW) / 2;
    int sy = pageHeight - metrics.buttonHintsHeight - 14 - DS;
    for (int p = 0; p < totalPages; ++p) {
      int dx = sx + p * (DS + DSp);
      if (p == curPage - 1) renderer.fillRect(dx, sy, DS, DS, true);
      else renderer.drawRect(dx, sy, DS, DS, true);
    }
  }

  // Draw popup ON TOP of grid while covers are generating (per-page counter)
  if (!coversComplete_) {
    char pb[48];
    int pageStartIdx = (curPage - 1) * gridsPerPage_;
    int pageCountHere = std::min(gridsPerPage_, total - pageStartIdx);
    int pageEndIdx = pageStartIdx + pageCountHere;
    // Count missing on current page
    int missingOnPage = 0;
    for (int i = pageStartIdx; i < pageEndIdx; ++i) {
      std::string d = libraryCoverPath(entries_[i].path);
      if (entries_[i].coverPath.empty() || !Storage.exists(d.c_str())) missingOnPage++;
    }
    int done = pageCountHere - missingOnPage;
    snprintf(pb, sizeof(pb), "Copertine %d/%d", done, pageCountHere);
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    int tx = metrics.contentSidePadding + 40;
    int ty = contentTop + 110;
    renderer.drawText(UI_10_FONT_ID, tx, ty, pb, true, EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}