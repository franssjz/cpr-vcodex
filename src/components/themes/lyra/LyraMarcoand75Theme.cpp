#include "LyraMarcoand75Theme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "I18n.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "util/CoverRibbonBaker.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/heart.h"
#include "components/icons/heart24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/screensaver.h"
#include "components/icons/goalsmedal.h"
#include "components/icons/readingstats.h"
#include "components/icons/recentbooks.h"
#include "components/icons/heatmap.h"
#include "components/icons/cleanmonitor.h"
#include "components/icons/sleep.h"
#include "components/icons/bookshelf.h"
#include "components/icons/flashcardquiz.h"
#include "components/icons/readingprofile.h"
#include "components/icons/lostdevice.h"
#include "components/icons/opdsbrowser.h"
#include "components/icons/dictionary.h"
#include "components/icons/settings.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/trophy.h"
#include "components/icons/trophy24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
constexpr int kOverlap = 35;
constexpr int kCoverTopPad = 8;
constexpr int kDotSize = 8;
constexpr int kDotGap = 6;
constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;
constexpr int kSelectionLineW = 3;
constexpr int kCenterOutlineW = 4;
constexpr int kMenuIconSize = 32;
constexpr int kMenuIconPad = 14;
constexpr int kHighlightPad = 12;
constexpr int kVisibleMenuSlots = 5;

// Data panel layout
constexpr int kDotsToPanelGap = 10;
constexpr int kPanelInnerPad = 6;
constexpr int kRowH = 22;
constexpr int kColGap = 30;
constexpr int kChamfer = 4;
constexpr int kDashLen = 4;
constexpr int kDashGap = 3;

// Segment progress bar
constexpr int kProgSegW = 14;
constexpr int kProgSegH = 24;
constexpr int kProgSegGap = 4;
constexpr int kProgSegCount = 15;

int lastCarouselSelectorIndex = -1;

const uint8_t* iconForName(UIIcon icon, int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:  return Folder24Icon;
      case UIIcon::Text:    return Text24Icon;
      case UIIcon::Image:   return Image24Icon;
      case UIIcon::Book:    return Book24Icon;
      case UIIcon::File:    return File24Icon;
      case UIIcon::Trophy:  return Trophy24Icon;
      case UIIcon::Heart:   return Heart24Icon;
      case UIIcon::ScreenSaver: return ScreenSaverIcon;
      case UIIcon::Bookshelf: return BookshelfIcon32;
      case UIIcon::SleepMode: return SleepModeIcon32;
      case UIIcon::CleanMonitor: return CleanMonitorIcon32;
      case UIIcon::Heatmap: return HeatmapReadingIcon32;
      case UIIcon::FlashcardQuiz: return FlashcardQuizIcon32;
      case UIIcon::ReadingProfile: return ReadingProfileIcon32;
      case UIIcon::LostDevice: return LostDeviceIcon32;
      case UIIcon::OpdsBrowser: return OPDSBrowserIcon32;
      case UIIcon::Dictionary: return DictionaryIcon32;
      case UIIcon::GoalsMedal: return GoalsMedalIcon32;
      case UIIcon::ReadingStatsIcon: return ReadingStatsIcon32;
      case UIIcon::RecentBooks: return RecentBooksIcon32;
      default: return nullptr;
    }
  }
  if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:    return FolderIcon;
      case UIIcon::Book:      return BookIcon;
      case UIIcon::Recent:    return RecentIcon;
      case UIIcon::Settings:  return Settings2Icon;
      case UIIcon::Apps:      return SettingsIcon;
      case UIIcon::Transfer:  return TransferIcon;
      case UIIcon::Library:   return LibraryIcon;
      case UIIcon::Trophy:    return TrophyIcon;
      case UIIcon::Wifi:      return WifiIcon;
      case UIIcon::Hotspot:   return HotspotIcon;
      case UIIcon::Image:     return ImageIcon;
      case UIIcon::Heart:     return HeartIcon;
      case UIIcon::ScreenSaver: return ScreenSaverIcon;
      case UIIcon::Bookshelf: return BookshelfIcon32;
      case UIIcon::SleepMode: return SleepModeIcon32;
      case UIIcon::CleanMonitor: return CleanMonitorIcon32;
      case UIIcon::Heatmap: return HeatmapReadingIcon32;
      case UIIcon::FlashcardQuiz: return FlashcardQuizIcon32;
      case UIIcon::ReadingProfile: return ReadingProfileIcon32;
      case UIIcon::LostDevice: return LostDeviceIcon32;
      case UIIcon::OpdsBrowser: return OPDSBrowserIcon32;
      case UIIcon::Dictionary: return DictionaryIcon32;
      case UIIcon::GoalsMedal: return GoalsMedalIcon32;
      case UIIcon::ReadingStatsIcon: return ReadingStatsIcon32;
      case UIIcon::RecentBooks: return RecentBooksIcon32;
      default: return nullptr;
    }
  }
  return nullptr;
}

void drawCoverPlaceholder(GfxRenderer& renderer, int x, int y, int maxW, int maxH) {
  renderer.drawRoundedRect(x, y, maxW, maxH, 1, kCornerRadius, true);
  renderer.fillRoundedRect(x, y + maxH / 3, maxW, 2 * maxH / 3, kCornerRadius, false, false, true, true, Color::Black);
  renderer.drawIcon(CoverIcon, x + maxW / 2 - 16, y + 8, 32, 32);
}

// --- Data panel helpers ---

void drawAngularRect(const GfxRenderer& r, int x, int y, int w, int h, int c, int lw, bool st) {
  if (w < c * 2 || h < c * 2) { r.drawRect(x, y, w, h, lw, st); return; }
  int x2 = x + w, y2 = y + h;
  r.drawLine(x + c, y, x2 - c, y, lw, st);
  r.drawLine(x2, y + c, x2, y2 - c, lw, st);
  r.drawLine(x2 - c, y2, x + c, y2, lw, st);
  r.drawLine(x, y2 - c, x, y + c, lw, st);
  r.drawLine(x, y + c, x + c, y, 1, st);
  r.drawLine(x2 - c, y, x2, y + c, 1, st);
  r.drawLine(x2, y2 - c, x2 - c, y2, 1, st);
  r.drawLine(x + c, y2, x, y2 - c, 1, st);
}

void drawCyberPanel(const GfxRenderer& r, int x, int y, int w, int h, bool sel) {
  drawAngularRect(r, x, y, w, h, kChamfer, sel ? 2 : 1, true);
  if (sel) drawAngularRect(r, x + 3, y + 3, w - 6, h - 6, kChamfer - 1, 1, true);
  int cl = 5, cg = 2;
  r.drawLine(x + cg, y + cg, x + cg + cl, y + cg, 1, true);
  r.drawLine(x + cg, y + cg, x + cg, y + cg + cl, 1, true);
  r.drawLine(x + w - cg - cl, y + cg, x + w - cg, y + cg, 1, true);
  r.drawLine(x + w - cg, y + cg, x + w - cg, y + cg + cl, 1, true);
  r.drawLine(x + cg, y + h - cg, x + cg + cl, y + h - cg, 1, true);
  r.drawLine(x + cg, y + h - cg - cl, x + cg, y + h - cg, 1, true);
  r.drawLine(x + w - cg - cl, y + h - cg, x + w - cg, y + h - cg, 1, true);
  r.drawLine(x + w - cg, y + h - cg - cl, x + w - cg, y + h - cg, 1, true);
}

void drawScanlineSep(const GfxRenderer& r, int x, int y, int w) {
  for (int cx = x; cx + kDashLen < x + w; cx += kDashLen + kDashGap) {
    r.drawLine(cx, y, cx + kDashLen, y, 1, true);
    r.drawLine(cx + 1, y + 2, cx + kDashLen - 1, y + 2, 1, true);
  }
}

void drawSegmentProgressBar(const GfxRenderer& r, int x, int y, int filled, int total) {
  int sx = x;
  for (int i = 0; i < total; ++i) {
    if (i < filled) r.fillRect(sx, y, kProgSegW, kProgSegH, true);
    else r.drawRect(sx, y, kProgSegW, kProgSegH, true);
    sx += kProgSegW + kProgSegGap;
  }
}

uint8_t getBookProgress(const RecentBook& b) {
  const ReadingBookStats* s = nullptr;
  if (!b.bookId.empty()) s = READING_STATS.findBook(b.bookId);
  if (!s) s = READING_STATS.findBook(b.path);
  return s ? std::min<uint8_t>(s->lastProgressPercent, 100) : 0;
}

const ReadingBookStats* getBookStats(const RecentBook& b) {
  const ReadingBookStats* s = nullptr;
  if (!b.bookId.empty()) s = READING_STATS.findBook(b.bookId);
  if (!s) s = READING_STATS.findBook(b.path);
  return s;
}

std::string fmtDuration(uint64_t ms) {
  if (ms == 0) return "0m";
  uint64_t m = ms / 60000ULL, h = m / 60;
  m %= 60;
  return h ? (std::to_string(h) + "h" + std::to_string(m) + "m") : (std::to_string(m) + "m");
}

std::string getEta(const ReadingBookStats& s) {
  if (s.completed || s.lastProgressPercent >= 100 || s.totalReadingMs < 600000ULL || s.lastProgressPercent < 5)
    return "";
  uint64_t tot = (s.totalReadingMs * 100ULL + s.lastProgressPercent - 1) / s.lastProgressPercent;
  if (tot <= s.totalReadingMs) return "";
  uint64_t rem = ((tot - s.totalReadingMs + 299999ULL) / 300000ULL) * 300000ULL;
  uint64_t min = rem / 60000ULL, h = min / 60;
  min %= 60;
  return "~" + std::to_string(h) + "h" + std::to_string(min) + "m";
}

// --- Data panel builder ---

void drawDataPanel(const GfxRenderer& r, const RecentBook& book, bool inCar, int px, int py, int pw, int ph) {
  drawCyberPanel(r, px, py, pw, ph, inCar);
  const ReadingBookStats* stats = getBookStats(book);
  const uint8_t pct = getBookProgress(book);
  const bool done = stats && stats->completed;
  const uint64_t tMs = stats ? stats->totalReadingMs : 0;
  const uint32_t sess = stats ? stats->sessions : 0;
  const std::string eta = stats ? getEta(*stats) : "";
  const std::string timeVal = fmtDuration(tMs);
  const std::string sessVal = std::to_string(sess);
  const std::string goalVal = fmtDuration(getDailyReadingGoalMs());
  const std::string dayVal = fmtDuration(READING_STATS.getTodayReadingMs());
  const std::string streakVal = std::to_string(READING_STATS.getCurrentStreakDays()) + "d";
  const std::string etaVal = done ? "--" : (eta.empty() ? "..." : eta);
  char pbuf[8]; snprintf(pbuf, sizeof(pbuf), "%u%%", pct);
  const std::string booksFinished = std::to_string(READING_STATS.getBooksFinishedCount());

  int ry = py + kPanelInnerPad;
  int rx = px + kPanelInnerPad;
  int rw = pw - 2 * kPanelInnerPad;

  const auto tTrunc = r.truncatedText(UI_12_FONT_ID, book.title.c_str(), rw, EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, rx, ry, tTrunc.c_str(), true, EpdFontFamily::BOLD);
  ry += r.getLineHeight(UI_12_FONT_ID) + 4;

  if (!book.author.empty()) {
    const auto aTrunc = r.truncatedText(SMALL_FONT_ID, book.author.c_str(), rw);
    r.drawText(SMALL_FONT_ID, rx, ry, aTrunc.c_str(), true);
    ry += r.getLineHeight(SMALL_FONT_ID) + 4;
  }
  drawScanlineSep(r, rx, ry, rw);
  ry += 12;

  const int colW = (rw - kColGap) / 2;
  const int colLeft = rx, colRight = rx + colW + kColGap;
  const int dataFont = UI_10_FONT_ID;

  r.drawText(dataFont, colLeft, ry, tr(STR_HOME_PANEL_BOOK), true, EpdFontFamily::BOLD);
  r.drawText(dataFont, colRight, ry, tr(STR_HOME_PANEL_STATS), true, EpdFontFamily::BOLD);
  ry += r.getLineHeight(dataFont) + 4;

  struct Row { const char* label; const std::string& value; };
  const Row leftCol[] = {
    {tr(STR_HOME_PANEL_TIME), timeVal},
    {tr(STR_HOME_PANEL_SESSIONS), sessVal},
    {tr(STR_HOME_PANEL_PROGRESS), std::string(pbuf)},
    {tr(STR_HOME_PANEL_ETA), etaVal}};
  const Row rightCol[] = {
    {tr(STR_HOME_PANEL_TODAY), dayVal},
    {tr(STR_HOME_PANEL_GOAL), goalVal},
    {tr(STR_HOME_PANEL_STREAK), streakVal},
    {tr(STR_HOME_PANEL_FINISHED), booksFinished}};

  int ly = ry, rry = ry;
  for (int i = 0; i < 4; ++i) {
    char buf[64]; snprintf(buf, sizeof(buf), "%s:", leftCol[i].label);
    int labelW = r.getTextWidth(dataFont, buf);
    r.drawText(dataFont, colLeft, ly, buf, true);
    r.drawText(dataFont, colLeft + labelW + 3, ly, leftCol[i].value.c_str(), true, EpdFontFamily::BOLD);
    ly += kRowH;
  }
  for (int i = 0; i < 4; ++i) {
    char buf[64]; snprintf(buf, sizeof(buf), "%s:", rightCol[i].label);
    int labelW = r.getTextWidth(dataFont, buf);
    r.drawText(dataFont, colRight, rry, buf, true);
    r.drawText(dataFont, colRight + labelW + 3, rry, rightCol[i].value.c_str(), true, EpdFontFamily::BOLD);
    rry += kRowH;
  }

  const int barY = (ly > rry ? ly : rry) + 10;
  if (!done) {
    const int segs = (pct * kProgSegCount + 50) / 100;
    const int barTotalW = kProgSegCount * (kProgSegW + kProgSegGap) - kProgSegGap;
    const int barX = px + 8;
    drawSegmentProgressBar(r, barX, barY, segs, kProgSegCount);
    char pctBuf[8]; snprintf(pctBuf, sizeof(pctBuf), "%u%%", pct);
    int pctX = barX + barTotalW + 10;
    int pctY = barY + (kProgSegH - r.getLineHeight(UI_12_FONT_ID)) / 2;
    r.drawText(UI_12_FONT_ID, pctX, pctY, pctBuf, true, EpdFontFamily::BOLD);
  } else {
    r.drawText(UI_12_FONT_ID, px + pw / 2 - 35, barY + 4, "COMPLETED", true, EpdFontFamily::BOLD);
  }
}

// Kindle-style "Read" corner ribbon (top-right).
void drawReadRibbon(GfxRenderer& renderer, int coverX, int coverY, int coverW, int coverH) {
  (void)coverH;
  const int leg = std::max(20, std::min(coverW * 2 / 5, 44));
  const int rightX = coverX + coverW;
  for (int dy = 0; dy < leg; ++dy) {
    const int spanW = leg - dy;
    renderer.fillRect(rightX - spanW, coverY + dy, spanW, 1, true);
  }
  const char* label = "Read";
  const int tw = renderer.getTextWidth(SMALL_FONT_ID, label, EpdFontFamily::BOLD);
  const int th = renderer.getLineHeight(SMALL_FONT_ID);
  const int rowFromTop = leg / 3;
  const int avail = leg - rowFromTop;
  if (tw + 6 <= avail) {
    const int tx = rightX - (avail + tw) / 2;
    const int ty = coverY + rowFromTop - th / 2;
    renderer.drawText(SMALL_FONT_ID, tx, ty, label, false, EpdFontFamily::BOLD);
  } else {
    const int cx = rightX - leg / 3;
    const int cy = coverY + leg / 3;
    renderer.drawLine(cx - 5, cy, cx - 1, cy + 4, 2, false);
    renderer.drawLine(cx - 1, cy + 4, cx + 6, cy - 4, 2, false);
  }
}
}  // namespace

void LyraMarcoand75Theme::setPreRenderIndex(int index) { lastCarouselSelectorIndex = index; }

void LyraMarcoand75Theme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                              const std::vector<RecentBook>& recentBooks,
                                              const int selectorIndex, bool& coverRendered,
                                              bool& coverBufferStored, bool& bufferRestored,
                                              std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  if (recentBooks.empty()) { drawEmptyRecents(renderer, rect); return; }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool inCarouselRow = selectorIndex < bookCount;
  int centerIdx = inCarouselRow ? selectorIndex : (lastCarouselSelectorIndex >= 0 ? lastCarouselSelectorIndex : 0);
  centerIdx = std::max(0, std::min(centerIdx, bookCount - 1));
  if (centerIdx != lastCarouselSelectorIndex) { coverRendered = false; coverBufferStored = false; }

  const int screenW = renderer.getScreenWidth();
  const int centerTileY = rect.y + kCoverTopPad;
  const int sideTileY = centerTileY + (kCenterCoverH - kSideCoverH) / 2;
  const int centerX = (screenW - kCenterCoverW) / 2;
  const int leftX = centerX - kSideCoverW + kOverlap;
  const int rightX = centerX + kCenterCoverW - kOverlap;

  auto drawCover = [&](int bookIdx, int x, int y, int maxW, int maxH) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];
    bool hasCover = false;
    std::string thumbPath;
    if (!book.coverBmpPath.empty()) {
      thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, maxW, maxH);
      const std::string centerThumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCenterCoverW, kCenterCoverH);
      const std::string legacyThumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraMarcoand75Metrics::values.homeCoverHeight);
      if (!Storage.exists(thumbPath.c_str())) {
        if (Storage.exists(centerThumbPath.c_str())) thumbPath = centerThumbPath;
        else if (Storage.exists(legacyThumbPath.c_str())) thumbPath = legacyThumbPath;
      }
      FsFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          const float bmpRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
          const float tileRatio = static_cast<float>(maxW) / static_cast<float>(maxH);
          const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
          const float cropY = (bmpRatio < tileRatio) ? (1.0f - bmpRatio / tileRatio) : 0.0f;
          renderer.drawBitmap(bitmap, x, y, maxW, maxH, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(x, y, maxW, maxH, kCornerRadius, Color::White);
          hasCover = true;
        }
        file.close();
      }
    }
    if (!hasCover) { drawCoverPlaceholder(renderer, x, y, maxW, maxH); }
    if (hasCover && !thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      CoverRibbonBaker::bakeIntoFile(thumbPath, book.path);
    }
    const ReadingBookStats* readStats = nullptr;
    if (!book.bookId.empty()) readStats = READING_STATS.findBook(book.bookId);
    if (readStats == nullptr) readStats = READING_STATS.findBook(book.path);
    if (readStats != nullptr && readStats->completed) {
      drawReadRibbon(renderer, x, y, maxW, maxH);
    }
    return true;
  };

  if (!coverRendered) {
    lastCarouselSelectorIndex = centerIdx;
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    const int prevIdx = (centerIdx + bookCount - 1) % bookCount;
    const int nextIdx = (centerIdx + 1) % bookCount;
    if (bookCount >= 3 && drawCover(prevIdx, leftX, sideTileY, kSideCoverW, kSideCoverH))
      renderer.drawRoundedRect(leftX, sideTileY, kSideCoverW, kSideCoverH, 1, kCornerRadius, true);
    if (bookCount >= 2 && drawCover(nextIdx, rightX, sideTileY, kSideCoverW, kSideCoverH))
      renderer.drawRoundedRect(rightX, sideTileY, kSideCoverW, kSideCoverH, 1, kCornerRadius, true);
    renderer.fillRect(centerX - kCenterOutlineW, centerTileY - kCenterOutlineW,
                      kCenterCoverW + 2 * kCenterOutlineW, kCenterCoverH + 2 * kCenterOutlineW, false);
    drawCover(centerIdx, centerX, centerTileY, kCenterCoverW, kCenterCoverH);

    const int dotsY = centerTileY + kCenterCoverH + 8;
    const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerX + (kCenterCoverW - totalDotsW) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIdx) renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
      else renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
      dotX += kDotSize + kDotGap;
    }
    const int panelY = dotsY + kDotSize + kDotsToPanelGap;
    const int panelH = rect.y + rect.height - panelY - 6;
    const int panelX = rect.x + 8;
    const int panelW = rect.width - 16;
    drawDataPanel(renderer, recentBooks[centerIdx], inCarouselRow, panelX, panelY, panelW, panelH);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }
  const int outlineW = inCarouselRow ? kSelectionLineW : kThinOutlineW;
  renderer.drawRoundedRect(centerX, centerTileY, kCenterCoverW, kCenterCoverH, outlineW, kCornerRadius, true);
}

void LyraMarcoand75Theme::drawCarouselBorder(GfxRenderer& renderer, Rect rect, bool inCarouselRow) const {
  if (!inCarouselRow) return;
  const int centerTileY = rect.y + kCoverTopPad;
  const int centerX = (renderer.getScreenWidth() - kCenterCoverW) / 2;
  renderer.drawRoundedRect(centerX, centerTileY, kCenterCoverW, kCenterCoverH, kSelectionLineW, kCornerRadius, true);
}

void LyraMarcoand75Theme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                         const std::function<std::string(int index)>& buttonLabel,
                                         const std::function<UIIcon(int index)>& rowIcon,
                                         const std::function<std::string(int index)>& buttonSubtitle,
                                         const std::function<bool(int index)>& showAccessory) const {
  (void)rect; (void)buttonLabel; (void)buttonSubtitle; (void)showAccessory;
  if (buttonCount <= 0) return;
  const int visibleCount = std::min(buttonCount, kVisibleMenuSlots);
  const int safeSelectedIndex = (selectedIndex >= 0 && selectedIndex < buttonCount) ? selectedIndex : -1;
  const int maxWindowStart = std::max(0, buttonCount - visibleCount);
  int windowStart = 0;
  if (safeSelectedIndex >= 0) windowStart = std::clamp(safeSelectedIndex - visibleCount / 2, 0, maxWindowStart);
  const int screenW = renderer.getScreenWidth();
  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int tileW = screenW / visibleCount;
  const int rowY = renderer.getScreenHeight() - LyraMarcoand75Metrics::values.buttonHintsHeight - tileH;
  renderer.fillRect(0, rowY, screenW, tileH, false);
  for (int slot = 0; slot < visibleCount; ++slot) {
    const int i = windowStart + slot;
    const int tileX = slot * tileW;
    const int iconX = tileX + (tileW - kMenuIconSize) / 2;
    const int iconY = rowY + kMenuIconPad;
    if (safeSelectedIndex == i) {
      const int highlightSize = kMenuIconSize + 2 * kHighlightPad;
      const int highlightY = rowY + (tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad, highlightY, highlightSize, highlightSize, kCornerRadius, Color::Black);
    }
    if (rowIcon != nullptr) {
      const uint8_t* bmp = iconForName(rowIcon(i), kMenuIconSize);
      if (bmp != nullptr) {
        if (safeSelectedIndex == i) {
          if (renderer.isDarkMode()) renderer.drawIconBlack(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          else renderer.drawIconInverted(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
        } else {
          renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
        }
      }
    }
  }
  if (buttonCount > visibleCount) {
    const int midY = rowY + tileH / 2;
    if (windowStart > 0) {
      renderer.drawLine(10, midY, 20, midY - 9, 2, true);
      renderer.drawLine(10, midY, 20, midY + 9, 2, true);
    }
    if (windowStart + visibleCount < buttonCount) {
      renderer.drawLine(screenW - 10, midY, screenW - 20, midY - 9, 2, true);
      renderer.drawLine(screenW - 10, midY, screenW - 20, midY + 9, 2, true);
    }
  }
}

void LyraMarcoand75Theme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                   const std::function<std::string(int index)>& rowTitle,
                                   const std::function<std::string(int index)>& rowSubtitle,
                                   const std::function<UIIcon(int index)>& rowIcon,
                                   const std::function<std::string(int index)>& rowValue,
                                   bool highlightValue, const std::function<bool(int index)>& rowCompleted) const {
  LyraTheme::drawList(renderer, rect, itemCount, selectedIndex, rowTitle, rowSubtitle, rowIcon, rowValue, highlightValue, rowCompleted);
}

void LyraMarcoand75Theme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, bool selected) const {
  LyraTheme::drawTabBar(renderer, rect, tabs, selected);
}