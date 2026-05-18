#include "LyraVcodex2Theme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/file.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/heart.h"
#include "components/icons/heart24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings.h"
#include "components/icons/settings2.h"
#include "components/icons/text.h"
#include "components/icons/text24.h"
#include "components/icons/trophy.h"
#include "components/icons/trophy24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr int SP_4 = 4;
constexpr int SP_8 = 8;
constexpr int SP_12 = 12;
constexpr int SP_16 = 16;
constexpr int SP_24 = 24;
constexpr int CARD_RADIUS = 6;
constexpr int COVER_WIDTH = 96;
constexpr int FALLBACK_COVER_RATIO_W = 2;
constexpr int FALLBACK_COVER_RATIO_H = 3;
constexpr int SUMMARY_HEIGHT = 58;
constexpr int UP_NEXT_HEIGHT = 72;
constexpr int PROGRESS_BAR_HEIGHT = 8;
constexpr int LIST_ROW_GAP = 6;
constexpr int LIST_ROW_INSET = 12;
constexpr int LIST_VALUE_MAX_WIDTH = 190;
constexpr int LIST_ICON_SIZE = 24;
constexpr int LIST_MENU_ICON_SIZE = 32;

uint8_t progressForBook(const RecentBook& book) {
  const ReadingBookStats* stats = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  return stats ? (stats->completed ? 100 : std::min<uint8_t>(stats->lastProgressPercent, 100)) : 0;
}

std::string compactTime(const uint64_t ms) { return ReadingStatsAnalytics::formatDurationHm(ms); }

std::string formatHeroEstimate(const ReadingStatsAnalytics::TimeLeftEstimate& estimate) {
  if (!estimate.ready || estimate.remainingMs == 0) {
    return tr(STR_LEARNING);
  }
  return ReadingStatsAnalytics::formatCompactTimeLeftEstimate(estimate);
}

void drawMiniProgressBar(GfxRenderer& renderer, const Rect& rect, const uint8_t progressPercent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  const int fillWidth = std::max(0, (rect.width - 4) * std::min<int>(progressPercent, 100) / 100);
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4), true);
  }
}

void drawMetric(GfxRenderer& renderer, const int x, const int y, const int width, const char* label,
                const std::string& value) {
  renderer.drawText(SMALL_FONT_ID, x, y, label, true);
  const std::string valueText = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, y + 17, valueText.c_str(), true, EpdFontFamily::BOLD);
}

void drawLabelValueLine(GfxRenderer& renderer, const int x, const int y, const int width, const char* label,
                        const std::string& value) {
  const std::string text = std::string(label) + ": " + value;
  const std::string line = renderer.truncatedText(SMALL_FONT_ID, text.c_str(), width);
  renderer.drawText(SMALL_FONT_ID, x, y, line.c_str(), true);
}

void drawSummaryRow(GfxRenderer& renderer, const Rect& rect) {
  const int innerX = rect.x + SP_16;
  const int innerWidth = rect.width - SP_16 * 2;
  const int colWidth = innerWidth / 4;
  const std::string today = compactTime(READING_STATS.getTodayReadingMs());
  const std::string streak = std::to_string(READING_STATS.getCurrentStreakDays()) + "d";
  const std::string total = compactTime(READING_STATS.getTotalReadingMs());
  const std::string done = std::to_string(READING_STATS.getBooksFinishedCount());

  renderer.drawLine(innerX, rect.y, innerX + innerWidth, rect.y, true);
  const int metricY = rect.y + SP_8;
  drawMetric(renderer, innerX, metricY, colWidth - SP_8, "Today", today);
  drawMetric(renderer, innerX + colWidth, metricY, colWidth - SP_8, "Streak", streak);
  drawMetric(renderer, innerX + colWidth * 2, metricY, colWidth - SP_8, "Total", total);
  drawMetric(renderer, innerX + colWidth * 3, metricY, colWidth - SP_8, "Done", done);
}

void drawCardSurface(GfxRenderer& renderer, const Rect& rect, const bool selected) {
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, CARD_RADIUS,
                           selected ? Color::LightGray : Color::White);
}

Rect coverFrameForRatio(const int x, const int y, const int maxWidth, const int maxHeight, const int sourceW,
                        const int sourceH) {
  const int safeSourceW = sourceW > 0 ? sourceW : FALLBACK_COVER_RATIO_W;
  const int safeSourceH = sourceH > 0 ? sourceH : FALLBACK_COVER_RATIO_H;
  int frameW = std::min(maxWidth, static_cast<int>((static_cast<int64_t>(maxHeight) * safeSourceW) / safeSourceH));
  int frameH = static_cast<int>((static_cast<int64_t>(frameW) * safeSourceH) / safeSourceW);

  if (frameH > maxHeight) {
    frameH = maxHeight;
    frameW = static_cast<int>((static_cast<int64_t>(frameH) * safeSourceW) / safeSourceH);
  }

  frameW = std::max(1, std::min(frameW, maxWidth));
  frameH = std::max(1, std::min(frameH, maxHeight));
  return Rect{x + std::max(0, (maxWidth - frameW) / 2), y + std::max(0, (maxHeight - frameH) / 2), frameW, frameH};
}

bool drawCover(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
               const int height) {
  bool hasCover = false;
  Rect frame = coverFrameForRatio(x, y, width, height, 0, 0);
  if (!book.coverBmpPath.empty()) {
    const std::string coverBmpPath = UITheme::resolveCoverThumbPath(book.coverBmpPath, width, height);
    FsFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const int sourceW = std::max(1, bitmap.getWidth());
        const int sourceH = std::max(1, bitmap.getHeight());
        frame = coverFrameForRatio(x, y, width, height, sourceW, sourceH);
        renderer.drawBitmap(bitmap, frame.x, frame.y, frame.width, frame.height);
        hasCover = true;
      }
      file.close();
    }
  }

  renderer.drawRect(frame.x, frame.y, frame.width, frame.height, true);
  if (!hasCover) {
    renderer.fillRect(frame.x, frame.y + frame.height / 3, frame.width, frame.height * 2 / 3, true);
    renderer.drawIcon(CoverIcon, frame.x + (frame.width - 32) / 2, frame.y + std::max(8, frame.height / 4), 32, 32);
  }
  return hasCover;
}

void drawUpNextCard(GfxRenderer& renderer, const Rect& rect, const RecentBook& book, const bool selected) {
  drawCardSurface(renderer, rect, selected);
  const int labelX = rect.x + SP_16;
  const int titleX = rect.x + 112;
  const int titleW = rect.width - (titleX - rect.x) - SP_16;
  const uint8_t progressPercent = progressForBook(book);
  const std::string progressText = std::to_string(progressPercent) + "%";
  renderer.drawText(SMALL_FONT_ID, labelX, rect.y + SP_12, tr(STR_MENU_RECENT_BOOKS), true);
  renderer.drawText(UI_10_FONT_ID, labelX, rect.y + 44, progressText.c_str(), true, EpdFontFamily::BOLD);
  const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, book.title.c_str(), titleW, 2, EpdFontFamily::BOLD);
  int titleY = rect.y + 16;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, line.c_str(), true, EpdFontFamily::BOLD);
    titleY += renderer.getLineHeight(UI_10_FONT_ID);
  }
  const std::string author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), titleW);
  if (!author.empty() && titleY <= rect.y + rect.height - renderer.getLineHeight(SMALL_FONT_ID) - SP_8) {
    renderer.drawText(SMALL_FONT_ID, titleX, titleY + SP_4, author.c_str(), true);
  }
}

const uint8_t* iconForName(const UIIcon icon, const int size) {
  if (size <= 24) {
    switch (icon) {
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Heart:
        return Heart24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Trophy:
        return Trophy24Icon;
      default:
        return nullptr;
    }
  }

  switch (icon) {
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::File:
      return FileIcon;
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Heart:
      return HeartIcon;
    case UIIcon::Image:
      return Image24Icon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return SettingsIcon;
    case UIIcon::Text:
      return TextIcon;
    case UIIcon::Trophy:
      return TrophyIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    default:
      return nullptr;
  }
}

bool drawVcodex2Icon(const GfxRenderer& renderer, const UIIcon icon, const int x, const int y, const int size) {
  if (const uint8_t* iconBitmap = iconForName(icon, size); iconBitmap != nullptr) {
    renderer.drawIcon(iconBitmap, x, y, size, size);
    return true;
  }
  if (size > 24) {
    if (const uint8_t* fallbackBitmap = iconForName(icon, 24); fallbackBitmap != nullptr) {
      const int inset = (size - 24) / 2;
      renderer.drawIcon(fallbackBitmap, x + inset, y + inset, 24, 24);
      return true;
    }
  }
  return false;
}

void drawCompletedBadge(const GfxRenderer& renderer, const int x, const int y, const int size) {
  const int badgeSize = std::max(12, size);
  renderer.drawRoundedRect(x, y, badgeSize, badgeSize, 1, 3, true);
  renderer.drawLine(x + 3, y + badgeSize / 2, x + badgeSize / 2 - 1, y + badgeSize - 4, 2, true);
  renderer.drawLine(x + badgeSize / 2 - 1, y + badgeSize - 4, x + badgeSize - 3, y + 3, 2, true);
}
}  // namespace

void LyraVcodex2Theme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                const std::function<std::string(int index)>& rowTitle,
                                const std::function<std::string(int index)>& rowSubtitle,
                                const std::function<UIIcon(int index)>& rowIcon,
                                const std::function<std::string(int index)>& rowValue, bool highlightValue,
                                const std::function<bool(int index)>& rowCompleted) const {
  const int rowHeight =
      (rowSubtitle != nullptr) ? LyraVcodex2Metrics::values.listWithSubtitleRowHeight : LyraVcodex2Metrics::values.listRowHeight;
  const int pageItems = std::max(1, rect.height / rowHeight);
  const int totalPages = (itemCount + pageItems - 1) / pageItems;

  if (totalPages > 1) {
    const int scrollBarHeight = (rect.height * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((rect.height - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraVcodex2Metrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + rect.height, true);
    renderer.fillRect(scrollBarX - LyraVcodex2Metrics::values.scrollBarWidth, scrollBarY,
                      LyraVcodex2Metrics::values.scrollBarWidth, scrollBarHeight, true);
  }

  const int contentWidth =
      rect.width - (totalPages > 1 ? (LyraVcodex2Metrics::values.scrollBarWidth +
                                      LyraVcodex2Metrics::values.scrollBarRightOffset)
                                   : 1);
  const int rowX = rect.x + LyraVcodex2Metrics::values.contentSidePadding;
  const int rowWidth = contentWidth - LyraVcodex2Metrics::values.contentSidePadding * 2;
  if (selectedIndex >= 0 && itemCount > 0) {
    const int selectedY = rect.y + selectedIndex % pageItems * rowHeight + LIST_ROW_GAP / 2;
    renderer.fillRoundedRect(rowX, selectedY, rowWidth, rowHeight - LIST_ROW_GAP, CARD_RADIUS, Color::LightGray);
    renderer.drawRoundedRect(rowX + 2, selectedY + 2, rowWidth - 4, rowHeight - LIST_ROW_GAP - 4, 1, CARD_RADIUS,
                             true);
  }

  int textX = rowX + LIST_ROW_INSET;
  int textWidth = rowWidth - LIST_ROW_INSET * 2;
  int iconSize = 0;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? LIST_MENU_ICON_SIZE : LIST_ICON_SIZE;
    textX += iconSize + SP_8;
    textWidth -= iconSize + SP_8;
  }

  const int pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    const int rowContentY = itemY + LIST_ROW_GAP / 2;
    const int rowContentHeight = rowHeight - LIST_ROW_GAP;
    int rowTextWidth = textWidth;

    std::string valueText;
    int valueWidth = 0;
    if (rowValue != nullptr) {
      valueText = renderer.truncatedText(UI_10_FONT_ID, rowValue(i).c_str(), LIST_VALUE_MAX_WIDTH);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + SP_16;
      rowTextWidth -= valueWidth + SP_8;
    }

    const std::string item = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), rowTextWidth);
    const int titleY = rowSubtitle != nullptr ? rowContentY + 8 : rowContentY + (rowContentHeight - 20) / 2 + 1;
    renderer.drawText(UI_10_FONT_ID, textX, titleY, item.c_str(), true);

    if (rowIcon != nullptr) {
      const int iconX = rowX + LIST_ROW_INSET;
      const int iconY = rowContentY + (rowContentHeight - iconSize) / 2;
      if (rowCompleted != nullptr && rowCompleted(i)) {
        drawCompletedBadge(renderer, iconX + std::max(0, (iconSize - 16) / 2), iconY + std::max(0, (iconSize - 16) / 2),
                           16);
      } else {
        (void)drawVcodex2Icon(renderer, rowIcon(i), iconX, iconY, iconSize);
      }
    }

    if (rowSubtitle != nullptr) {
      const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, rowSubtitle(i).c_str(), rowTextWidth);
      renderer.drawText(SMALL_FONT_ID, textX, rowContentY + 35, subtitle.c_str(), true);
    }

    if (!valueText.empty()) {
      const int valueX = rect.x + contentWidth - LyraVcodex2Metrics::values.contentSidePadding - valueWidth;
      const int valueY = rowContentY + (rowContentHeight - 24) / 2;
      if (i == selectedIndex && highlightValue) {
        renderer.fillRoundedRect(valueX - 2, valueY - 2, valueWidth + 4, 28, CARD_RADIUS, Color::Black);
      }
      renderer.drawText(UI_10_FONT_ID, valueX + SP_8, valueY + 3, valueText.c_str(),
                        !(i == selectedIndex && highlightValue));
    }

  }
}

void LyraVcodex2Theme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int cardX = rect.x + LyraVcodex2Metrics::values.contentSidePadding;
  const int cardW = rect.width - LyraVcodex2Metrics::values.contentSidePadding * 2;
  const bool showUpNext = SETTINGS.showCurrentBookCard != 0 && recentBooks.size() > 1;
  const bool hasBook = !recentBooks.empty();

  if (!hasBook) {
    drawCardSurface(renderer, Rect{cardX, rect.y, cardW, rect.height - SUMMARY_HEIGHT - SP_12}, selectorIndex == 0);
    const int emptyY = rect.y + 58;
    renderer.drawText(UI_12_FONT_ID, cardX + SP_16, emptyY, tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, cardX + SP_16, emptyY + 28, tr(STR_START_READING), true);
    drawSummaryRow(renderer, Rect{cardX, rect.y + rect.height - SUMMARY_HEIGHT, cardW, SUMMARY_HEIGHT});
    return;
  }

  const RecentBook& book = recentBooks[0];
  const int extraH = showUpNext ? (UP_NEXT_HEIGHT + SP_8) : 0;
  const int heroH = rect.height - SUMMARY_HEIGHT - SP_12 - extraH;
  const int heroY = rect.y;
  const int coverX = cardX + SP_12;
  const int coverY = heroY + SP_16;
  const int coverH = std::min(LyraVcodex2Metrics::values.homeCoverHeight, heroH - SP_16 - SP_12);
  const int coverW = std::max(COVER_WIDTH, coverH * FALLBACK_COVER_RATIO_W / FALLBACK_COVER_RATIO_H);
  const int textX = coverX + coverW + SP_16;
  const int textW = cardW - (textX - cardX) - SP_12;

  drawCardSurface(renderer, Rect{cardX, heroY, cardW, heroH}, selectorIndex == 0);
  drawCover(renderer, book, coverX, coverY, coverW, coverH);
  coverRendered = false;
  coverBufferStored = false;
  bufferRestored = false;

  const uint8_t progressPercent = progressForBook(book);
  const std::string progressText = std::to_string(progressPercent) + "%";
  const int progressTextWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int titleW = std::max(20, textW - progressTextWidth - SP_8);
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), titleW, 2, EpdFontFamily::BOLD);
  int currentY = heroY + SP_12;
  renderer.drawText(SMALL_FONT_ID, textX, currentY, tr(STR_CONTINUE_READING), true);
  currentY += renderer.getLineHeight(SMALL_FONT_ID) + SP_4;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
    currentY += renderer.getLineHeight(UI_12_FONT_ID);
  }
  renderer.drawText(UI_12_FONT_ID, cardX + cardW - SP_12 - progressTextWidth, heroY + SP_12, progressText.c_str(), true,
                    EpdFontFamily::BOLD);

  if (!book.author.empty()) {
    currentY += SP_4;
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textW);
    renderer.drawText(UI_10_FONT_ID, textX, currentY, author.c_str(), true);
    currentY += renderer.getLineHeight(UI_10_FONT_ID);
  }

  currentY = std::min(currentY + SP_12, heroY + heroH - 58);
  drawMiniProgressBar(renderer, Rect{textX, currentY, textW, PROGRESS_BAR_HEIGHT}, progressPercent);

  const ReadingBookStats* stats = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  if (stats != nullptr) {
    currentY += PROGRESS_BAR_HEIGHT + SP_12;
    drawLabelValueLine(renderer, textX, currentY, textW, tr(STR_TOTAL_READ), compactTime(stats->totalReadingMs));

    currentY += renderer.getLineHeight(SMALL_FONT_ID) + SP_4;
    const auto estimate = ReadingStatsAnalytics::buildBookTimeLeftEstimate(*stats);
    drawLabelValueLine(renderer, textX, currentY, textW, tr(STR_ESTIMATED_TIME_LEFT), formatHeroEstimate(estimate));
  }

  if (showUpNext) {
    const int upNextY = heroY + heroH + SP_8;
    drawUpNextCard(renderer, Rect{cardX, upNextY, cardW, UP_NEXT_HEIGHT}, recentBooks[1], selectorIndex == 1);
  }

  drawSummaryRow(renderer, Rect{cardX, rect.y + rect.height - SUMMARY_HEIGHT, cardW, SUMMARY_HEIGHT});
}
