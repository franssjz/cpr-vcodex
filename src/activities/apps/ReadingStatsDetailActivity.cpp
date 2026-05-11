#include "ReadingStatsDetailActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "BookReadingAdjustmentActivity.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "components/icons/settings2.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int COVER_WIDTH = 96;
constexpr int COVER_HEIGHT = 140;
constexpr int DONUT_RADIUS = 54;
constexpr int DONUT_THICKNESS = 9;
constexpr int METRIC_ROW_HEIGHT = 46;
constexpr int DETAIL_FOCUS_ITEM_COUNT = 2;
constexpr int DETAIL_ADJUST_FOCUS_INDEX = 1;
constexpr int ADJUST_BUTTON_SIZE = 54;
constexpr int SUMMARY_BANNER_HEIGHT = 46;
constexpr int SUMMARY_BANNER_GAP = 8;
constexpr int DETAIL_SCROLL_STEP = 128;
constexpr size_t MAX_RESOLVED_COVERS = 16;

struct ResolvedCoverCacheEntry {
  std::string bookPath;
  std::string coverBmpPath;
  std::string resolvedPath;
};

std::vector<ResolvedCoverCacheEntry>& getResolvedCoverCache() {
  static std::vector<ResolvedCoverCacheEntry> cache;
  return cache;
}

std::string getCachedResolvedCoverPath(const ReadingBookStats& book) {
  auto& cache = getResolvedCoverCache();
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (it->bookPath != book.path || it->coverBmpPath != book.coverBmpPath) {
      continue;
    }
    if (!it->resolvedPath.empty() && Storage.exists(it->resolvedPath.c_str())) {
      if (it != cache.begin()) {
        ResolvedCoverCacheEntry entry = *it;
        cache.erase(it);
        cache.insert(cache.begin(), std::move(entry));
      }
      return cache.front().resolvedPath;
    }
    break;
  }
  return "";
}

void rememberResolvedCoverPath(const ReadingBookStats& book, const std::string& resolvedPath) {
  if (resolvedPath.empty()) {
    return;
  }

  auto& cache = getResolvedCoverCache();
  cache.erase(std::remove_if(cache.begin(), cache.end(),
                             [&](const ResolvedCoverCacheEntry& entry) { return entry.bookPath == book.path; }),
              cache.end());
  cache.insert(cache.begin(), ResolvedCoverCacheEntry{book.path, book.coverBmpPath, resolvedPath});
  if (cache.size() > MAX_RESOLVED_COVERS) {
    cache.pop_back();
  }
}

ReadingBookStats withCoverPath(const ReadingBookStats& book, const std::string& coverBmpPath) {
  ReadingBookStats updated = book;
  updated.coverBmpPath = coverBmpPath;
  return updated;
}

const ReadingBookStats* findBook(const std::string& bookPath) {
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.path == bookPath) {
      return &book;
    }
  }
  return nullptr;
}

std::string resolveStoredCoverPath(const std::string& coverBmpPath) {
  if (coverBmpPath.empty()) {
    return "";
  }

  if (coverBmpPath.find("[HEIGHT]") != std::string::npos) {
    const int candidateHeights[] = {COVER_HEIGHT, 160, 240, 400};
    for (const int height : candidateHeights) {
      const std::string resolved = UITheme::getCoverThumbPath(coverBmpPath, height);
      if (Storage.exists(resolved.c_str())) {
        return resolved;
      }
    }
    return "";
  }

  return Storage.exists(coverBmpPath.c_str()) ? coverBmpPath : "";
}

std::string ensureCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  if (!Storage.exists(book.path.c_str())) {
    return "";
  }

  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    if (!epub.load(true, true)) {
      return "";
    }
    epub.setupCacheDir();
    const std::string coverPath = epub.getCoverBmpPath();
    if (!Storage.exists(coverPath.c_str()) && !epub.generateCoverBmp()) {
      return "";
    }
    if (!Storage.exists(coverPath.c_str())) {
      return "";
    }
    READING_STATS.updateBookMetadata(book.path, epub.getTitle(), epub.getAuthor(), coverPath);
    rememberResolvedCoverPath(withCoverPath(book, coverPath), coverPath);
    return coverPath;
  }

  if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    if (!xtc.load()) {
      return "";
    }
    xtc.setupCacheDir();
    const std::string coverPath = xtc.getCoverBmpPath();
    if (!Storage.exists(coverPath.c_str()) && !xtc.generateCoverBmp()) {
      return "";
    }
    if (!Storage.exists(coverPath.c_str())) {
      return "";
    }
    READING_STATS.updateBookMetadata(book.path, xtc.getTitle(), xtc.getAuthor(), coverPath);
    rememberResolvedCoverPath(withCoverPath(book, coverPath), coverPath);
    return coverPath;
  }

  if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    Txt txt(book.path, "/.crosspoint");
    if (!txt.load()) {
      return "";
    }
    txt.setupCacheDir();
    const std::string coverPath = txt.getCoverBmpPath();
    if (!Storage.exists(coverPath.c_str()) && !txt.generateCoverBmp()) {
      return "";
    }
    if (!Storage.exists(coverPath.c_str())) {
      return "";
    }
    READING_STATS.updateBookMetadata(book.path, txt.getTitle(), "", coverPath);
    rememberResolvedCoverPath(withCoverPath(book, coverPath), coverPath);
    return coverPath;
  }

  return "";
}

std::string findFastCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  if (!Storage.exists(book.path.c_str())) {
    return "";
  }

  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(epub.getCoverBmpPath());
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(xtc.getCoverBmpPath());
  } else if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    Txt txt(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(txt.getCoverBmpPath());
  }

  if (!resolved.empty()) {
    READING_STATS.updateBookMetadata(book.path, "", "", resolved);
    rememberResolvedCoverPath(withCoverPath(book, resolved), resolved);
  }
  return resolved;
}

std::string getDisplayTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string formatDate(const uint32_t timestamp) {
  const std::string formatted = TimeUtils::formatDate(timestamp);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string formatDateRange(const uint32_t startTimestamp, const uint32_t endTimestamp) {
  const std::string start = TimeUtils::formatDate(startTimestamp);
  const std::string end = TimeUtils::formatDate(endTimestamp);
  return (start.empty() ? "?" : start) + " - " + (end.empty() ? "?" : end);
}

uint32_t getCompletionDateForDisplay(const ReadingBookStats& book) { return book.completedAt; }

bool pointInProgressArc(const int dx, const int dy, const float progress) {
  constexpr float kPi = 3.1415926535f;
  if (progress >= 0.999f) {
    return true;
  }
  float angle = atan2f(static_cast<float>(dy), static_cast<float>(dx)) + kPi / 2.0f;
  while (angle < 0.0f) {
    angle += kPi * 2.0f;
  }
  const float normalized = angle / (kPi * 2.0f);
  return normalized <= progress;
}

void drawDonutGauge(GfxRenderer& renderer, const int cx, const int cy, const int radius, const int thickness,
                    const uint8_t percent) {
  const int innerRadius = std::max(2, radius - thickness);
  const int outer2 = radius * radius;
  const int inner2 = innerRadius * innerRadius;
  const float progress = std::min<int>(percent, 100) / 100.0f;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      const int d2 = x * x + y * y;
      if (d2 <= inner2 || d2 > outer2) {
        continue;
      }
      if (pointInProgressArc(x, y, progress)) {
        renderer.drawPixel(cx + x, cy + y, true);
      } else if (((x + y) & 3) == 0) {
        renderer.drawPixel(cx + x, cy + y, true);
      }
    }
  }
  const std::string percentText = std::to_string(std::min<int>(percent, 100)) + "%";
  renderer.drawText(UI_12_FONT_ID, cx - 18, cy - 8, percentText.c_str(), true, EpdFontFamily::BOLD);
}

void drawAdjustTimeButton(GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, true);

  constexpr int iconSize = 32;
  const int iconX = rect.x + (rect.width - iconSize) / 2;
  const int iconY = rect.y + (rect.height - iconSize) / 2;
  renderer.drawIcon(Settings2Icon, iconX, iconY, iconSize, iconSize);
}

Rect offsetRect(Rect rect, const int dy) {
  rect.y += dy;
  return rect;
}

void drawSummaryBanner(GfxRenderer& renderer, const Rect& rect, const char* title, const std::string& summary,
                       const bool inverted = false) {
  if (inverted) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 6, Color::Black);
  } else {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }

  renderer.drawText(UI_10_FONT_ID, rect.x + 10, rect.y + 6, title, !inverted, EpdFontFamily::BOLD);
  const auto summaryLines =
      renderer.wrappedText(UI_10_FONT_ID, summary.c_str(), rect.width - 20, 2, EpdFontFamily::REGULAR);
  int summaryY = rect.y + 23;
  for (const auto& line : summaryLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 10, summaryY, line.c_str(), !inverted, EpdFontFamily::REGULAR);
    summaryY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void drawStatsTableRow(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                       const bool selected = false) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);

  const int labelWidth = (rect.width * 42) / 100;
  const int valueX = rect.x + labelWidth + 8;
  const int valueWidth = rect.width - labelWidth - 18;
  const std::string labelText = renderer.truncatedText(UI_10_FONT_ID, label, labelWidth - 12, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + 10, rect.y + 14, labelText.c_str(), true, EpdFontFamily::BOLD);

  const auto lines = renderer.wrappedText(UI_10_FONT_ID, value.c_str(), valueWidth, 2, EpdFontFamily::REGULAR);
  int y = rect.y + (lines.size() > 1 ? 5 : 14);
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, valueX, y, line.c_str(), true, EpdFontFamily::REGULAR);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void drawProgressBlock(GfxRenderer& renderer, const Rect& rect, const char* label, const uint8_t percent) {
  const std::string percentText = std::to_string(std::min<int>(percent, 100)) + "%";
  const int percentWidth = renderer.getTextWidth(UI_10_FONT_ID, percentText.c_str(), EpdFontFamily::BOLD);

  renderer.drawText(UI_10_FONT_ID, rect.x, rect.y, label);
  renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - percentWidth, rect.y, percentText.c_str(), true,
                    EpdFontFamily::BOLD);

  const Rect barRect{rect.x, rect.y + 23, rect.width, 10};
  renderer.drawRect(barRect.x, barRect.y, barRect.width, barRect.height);
  const int fillWidth = std::max(0, barRect.width - 4) * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(barRect.x + 2, barRect.y + 2, fillWidth, std::max(0, barRect.height - 4));
  }
}

void drawCover(GfxRenderer& renderer, const Rect& rect, const std::string& coverPath) {
  const auto drawFallback = [&renderer, &rect]() {
    const char* label = tr(STR_BOOK);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int textY = rect.y + rect.height / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
  };

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  if (coverPath.empty()) {
    drawFallback();
    return;
  }

  FsFile file;
  if (!Storage.openFileForRead("RSD", coverPath, file)) {
    drawFallback();
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    renderer.drawBitmap(bitmap, rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4);
  } else {
    drawFallback();
  }
  file.close();
}
}  // namespace

void ReadingStatsDetailActivity::onEnter() {
  Activity::onEnter();
  invalidateBaseScreenBuffer();
  resolvedCoverBmpPath.clear();
  coverLoadPending = false;
  selectedStatsItem = 0;
  scrollOffset = 0;
  maxScrollOffset = 0;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  if (const auto* book = findBook(bookPath)) {
    resolvedCoverBmpPath = findFastCoverPath(*book);
    coverLoadPending = resolvedCoverBmpPath.empty();
  }
  requestUpdate();
}

void ReadingStatsDetailActivity::onExit() {
  Activity::onExit();
  freeBaseScreenBuffer();
}

bool ReadingStatsDetailActivity::storeBaseScreenBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeBaseScreenBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  baseScreenBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!baseScreenBuffer) {
    return false;
  }

  memcpy(baseScreenBuffer, frameBuffer, bufferSize);
  baseScreenBufferStored = true;
  baseScreenBookPath = bookPath;
  baseScreenCoverPath = resolvedCoverBmpPath;
  baseScreenScrollOffset = scrollOffset;
  return true;
}

bool ReadingStatsDetailActivity::restoreBaseScreenBuffer() {
  if (!baseScreenBufferStored || !baseScreenBuffer || baseScreenBookPath != bookPath ||
      baseScreenCoverPath != resolvedCoverBmpPath || baseScreenScrollOffset != scrollOffset) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  memcpy(frameBuffer, baseScreenBuffer, renderer.getBufferSize());
  return true;
}

void ReadingStatsDetailActivity::invalidateBaseScreenBuffer() {
  baseScreenBufferStored = false;
  baseScreenBookPath.clear();
  baseScreenCoverPath.clear();
  baseScreenScrollOffset = -1;
}

void ReadingStatsDetailActivity::freeBaseScreenBuffer() {
  if (baseScreenBuffer) {
    free(baseScreenBuffer);
    baseScreenBuffer = nullptr;
  }
  invalidateBaseScreenBuffer();
}

void ReadingStatsDetailActivity::openAdjustment() {
  const auto* book = findBook(bookPath);
  if (book == nullptr) {
    requestUpdate();
    return;
  }

  startActivityForResult(
      std::make_unique<BookReadingAdjustmentActivity>(renderer, mappedInput, book->path, getDisplayTitle(*book)),
      [this](const ActivityResult&) {
        guardChildReturn();
        requestUpdate();
      });
}

void ReadingStatsDetailActivity::guardChildReturn() {
  invalidateBaseScreenBuffer();
  waitForBackRelease = true;
  waitForConfirmRelease = true;
}

void ReadingStatsDetailActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  const auto scrollBy = [&](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset == scrollOffset) {
      return false;
    }
    scrollOffset = nextOffset;
    selectedStatsItem = 0;
    invalidateBaseScreenBuffer();
    requestUpdate();
    return true;
  };

  buttonNavigator.onNextPress([&]() {
    if (maxScrollOffset > 0) {
      if (scrollOffset == 0 && selectedStatsItem == 0) {
        selectedStatsItem = DETAIL_ADJUST_FOCUS_INDEX;
        requestUpdate();
        return;
      }
      if (scrollOffset < maxScrollOffset && scrollBy(DETAIL_SCROLL_STEP)) {
        return;
      }
      return;
    }

    selectedStatsItem = ButtonNavigator::nextIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([&]() {
    if (maxScrollOffset > 0) {
      if (scrollOffset > 0 && scrollBy(-DETAIL_SCROLL_STEP)) {
        return;
      }
      selectedStatsItem = ButtonNavigator::previousIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
      requestUpdate();
      return;
    }

    selectedStatsItem = ButtonNavigator::previousIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
    requestUpdate();
  });

  if (coverLoadPending) {
    coverLoadPending = false;
    if (const auto* book = findBook(bookPath)) {
      const std::string resolvedCoverPath = ensureCoverPath(*book);
      if (!resolvedCoverPath.empty() && resolvedCoverPath != resolvedCoverBmpPath) {
        resolvedCoverBmpPath = resolvedCoverPath;
        invalidateBaseScreenBuffer();
        requestUpdate();
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && selectedStatsItem == DETAIL_ADJUST_FOCUS_INDEX) {
    openAdjustment();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && Storage.exists(bookPath.c_str())) {
    onSelectBook(bookPath);
  }
}

void ReadingStatsDetailActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const auto* book = findBook(bookPath);
  const auto& lastSessionSnapshot = READING_STATS.getLastSessionSnapshot();
  const bool showCompletionBanner = context.showSessionSummary && lastSessionSnapshot.valid &&
                                    lastSessionSnapshot.path == bookPath && lastSessionSnapshot.completedThisSession;

  if (!book) {
    renderer.clearScreen();
    invalidateBaseScreenBuffer();
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_READING_STATS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect coverBaseRect{metrics.contentSidePadding, contentTop, COVER_WIDTH, COVER_HEIGHT};
  const Rect adjustButtonBaseRect{coverBaseRect.x + (coverBaseRect.width - ADJUST_BUTTON_SIZE) / 2,
                                  coverBaseRect.y + coverBaseRect.height + metrics.verticalSpacing,
                                  ADJUST_BUTTON_SIZE, ADJUST_BUTTON_SIZE};
  const int textX = coverBaseRect.x + coverBaseRect.width + 16;
  const int textWidth = pageWidth - textX - metrics.contentSidePadding;

  int currentY = contentTop + 6;
  const int titleTop = currentY;
  const auto wrappedTitle =
      renderer.wrappedText(UI_12_FONT_ID, getDisplayTitle(*book).c_str(), textWidth, 2, EpdFontFamily::BOLD);
  currentY += static_cast<int>(wrappedTitle.size()) * renderer.getLineHeight(UI_12_FONT_ID);

  const int authorTop = currentY + 4;
  if (!book->author.empty()) {
    currentY += renderer.getLineHeight(UI_10_FONT_ID) + 10;
  } else {
    currentY += 10;
  }

  const std::string currentChapter = book->chapterTitle.empty() ? std::string(tr(STR_NOT_SET)) : book->chapterTitle;
  currentY = std::max(currentY + 10, contentTop + DONUT_RADIUS * 2 + 18);

  int cardsTop = std::max(adjustButtonBaseRect.y + adjustButtonBaseRect.height, currentY) + metrics.verticalSpacing + 10;
  const int summaryBannerTop = cardsTop;
  if (showCompletionBanner) {
    cardsTop += SUMMARY_BANNER_HEIGHT + SUMMARY_BANNER_GAP;
  }

  constexpr int metricRowCount = 16;
  const int contentBottom = cardsTop + metricRowCount * METRIC_ROW_HEIGHT + metrics.verticalSpacing;
  maxScrollOffset = std::max(0, contentBottom - viewportBottom);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const int scrollDy = -scrollOffset;
  const Rect coverRect = offsetRect(coverBaseRect, scrollDy);
  const Rect adjustButtonRect = offsetRect(adjustButtonBaseRect, scrollDy);
  const bool adjustSelected = scrollOffset == 0 && selectedStatsItem == DETAIL_ADJUST_FOCUS_INDEX;

  const bool baseScreenRestored = restoreBaseScreenBuffer();
  if (!baseScreenRestored) {
    renderer.clearScreen();
    drawCover(renderer, coverRect, resolvedCoverBmpPath);
    drawAdjustTimeButton(renderer, adjustButtonRect, false);

    currentY = titleTop + scrollDy;
    for (const auto& line : wrappedTitle) {
      renderer.drawText(UI_12_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
      currentY += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!book->author.empty()) {
      renderer.drawText(UI_10_FONT_ID, textX, authorTop + scrollDy, book->author.c_str());
    }

    drawDonutGauge(renderer, textX + textWidth / 2, contentTop + DONUT_RADIUS + 18 + scrollDy, DONUT_RADIUS,
                   DONUT_THICKNESS, book->lastProgressPercent);

    int drawCardsTop = cardsTop + scrollDy;
    if (showCompletionBanner) {
      drawSummaryBanner(
          renderer,
          Rect{metrics.contentSidePadding, summaryBannerTop + scrollDy, pageWidth - metrics.contentSidePadding * 2,
               SUMMARY_BANNER_HEIGHT},
          tr(STR_BOOK_FINISHED), tr(STR_COMPLETED_THIS_SESSION), true);
    }

    const auto bookEstimate = ReadingStatsAnalytics::buildBookTimeLeftEstimate(*book);
    const auto chapterEstimate = ReadingStatsAnalytics::buildChapterTimeLeftEstimate(*book);
    const std::string chapterEstimateValue =
        chapterEstimate.ready && chapterEstimate.confidence != ReadingStatsAnalytics::EstimateConfidence::LOW_CONFIDENCE
            ? ReadingStatsAnalytics::formatTimeLeftEstimate(chapterEstimate)
            : ReadingStatsAnalytics::formatTimeLeftEstimate(bookEstimate);
    const std::string progressGainValue =
        std::to_string(ReadingStatsAnalytics::getTrackedProgressGainPercent(*book)) + "%";
    const std::string paceTrendValue = ReadingStatsAnalytics::formatPaceTrend(*book);
    const std::string confidenceValue =
        bookEstimate.ready ? ReadingStatsAnalytics::formatEstimateConfidence(bookEstimate.confidence)
                           : ReadingStatsAnalytics::formatEstimateReadinessExplanation(bookEstimate);
    const std::string sessionProgressValue =
        context.showSessionSummary && lastSessionSnapshot.valid && lastSessionSnapshot.path == bookPath
            ? std::to_string(lastSessionSnapshot.endProgressPercent > lastSessionSnapshot.startProgressPercent
                                 ? lastSessionSnapshot.endProgressPercent - lastSessionSnapshot.startProgressPercent
                                 : 0) +
                  "%"
            : progressGainValue;

    const Rect tableRect{metrics.contentSidePadding, drawCardsTop, pageWidth - metrics.contentSidePadding * 2,
                         metricRowCount * METRIC_ROW_HEIGHT};
    renderer.drawRect(tableRect.x, tableRect.y, tableRect.width, tableRect.height);
    int rowIndex = 0;
    const auto drawRow = [&](const char* label, const std::string& value) {
      drawStatsTableRow(renderer,
                        Rect{tableRect.x, tableRect.y + rowIndex * METRIC_ROW_HEIGHT, tableRect.width,
                             METRIC_ROW_HEIGHT},
                        label, value);
      rowIndex++;
    };
    drawRow(tr(STR_LAST_SESSION), ReadingStatsAnalytics::formatDurationHm(book->lastSessionMs));
    drawRow(tr(STR_TOTAL_TIME), ReadingStatsAnalytics::formatDurationHm(book->totalReadingMs));
    drawRow(tr(STR_BOOK_PROGRESS), std::to_string(book->lastProgressPercent) + "%");
    drawRow(tr(STR_CHAPTER_PROGRESS), std::to_string(book->chapterProgressPercent) + "%");
    drawRow(tr(STR_CURRENT_CHAPTER), currentChapter);
    drawRow(tr(STR_BOOK_TIME_LEFT), ReadingStatsAnalytics::formatTimeLeftEstimate(bookEstimate));
    drawRow(tr(STR_CHAPTER_TIME_LEFT), chapterEstimateValue);
    drawRow(tr(STR_AVG_PACE),
            ReadingStatsAnalytics::formatProgressPace(ReadingStatsAnalytics::getAverageProgressPaceTenths(*book)));
    drawRow(tr(STR_RECENT_PACE),
            ReadingStatsAnalytics::formatProgressPace(ReadingStatsAnalytics::getRecentProgressPaceTenths(*book)));
    drawRow(tr(STR_PROGRESS_GAIN), progressGainValue);
    drawRow(tr(STR_PACE_TREND), paceTrendValue);
    drawRow(context.showSessionSummary ? tr(STR_SESSION_PROGRESS) : tr(STR_SESSIONS),
            context.showSessionSummary ? sessionProgressValue : std::to_string(book->sessions));
    drawRow(tr(STR_CONFIDENCE), confidenceValue);
    drawRow(tr(STR_ESTIMATE_STABILITY), ReadingStatsAnalytics::formatEstimateStability(*book));
    drawRow(tr(STR_SESSION_PACE),
            ReadingStatsAnalytics::formatProgressPace(
                lastSessionSnapshot.valid && lastSessionSnapshot.path == bookPath && lastSessionSnapshot.sessionMs > 0 &&
                        lastSessionSnapshot.endProgressPercent > lastSessionSnapshot.startProgressPercent
                    ? static_cast<uint32_t>(((lastSessionSnapshot.endProgressPercent -
                                              lastSessionSnapshot.startProgressPercent) *
                                                 36000ULL +
                                             lastSessionSnapshot.sessionMs / 2) /
                                            lastSessionSnapshot.sessionMs)
                    : 0));
    drawRow(tr(STR_START_END_DATE), formatDateRange(book->firstReadAt, getCompletionDateForDisplay(*book)));

    renderer.fillRect(0, 0, pageWidth, contentTop, false);
    if (viewportBottom < pageHeight) {
      renderer.fillRect(0, viewportBottom, pageWidth, pageHeight - viewportBottom, false);
    }
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

    storeBaseScreenBuffer();
  }

  if (adjustSelected) {
    drawAdjustTimeButton(renderer, adjustButtonRect, true);
  }

  const char* confirmLabel = adjustSelected ? tr(STR_ADJUST) : (Storage.exists(bookPath.c_str()) ? tr(STR_OPEN) : "");
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
