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
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int COVER_WIDTH = 96;
constexpr int COVER_HEIGHT = 140;
constexpr int PROGRESS_BLOCK_HEIGHT = 38;
constexpr int METRIC_CARD_HEIGHT = 78;
constexpr int METRIC_CARD_GAP = 8;
constexpr int METRIC_CARD_VALUE_Y = 14;
constexpr int METRIC_CARD_LABEL_Y = 50;
constexpr int SUMMARY_BANNER_HEIGHT = 46;
constexpr int SUMMARY_BANNER_GAP = 8;
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

std::string formatDurationHm(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
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

int findBookIndex(const std::string& bookPath) {
  const auto& books = READING_STATS.getBooks();
  for (int index = 0; index < static_cast<int>(books.size()); ++index) {
    if (books[index].path == bookPath) {
      return index;
    }
  }
  return -1;
}

std::string formatDate(const uint32_t timestamp) {
  const std::string formatted = TimeUtils::formatDate(timestamp);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string formatDateRange(const uint32_t startTimestamp, const uint32_t endTimestamp) {
  const std::string start = TimeUtils::formatDate(startTimestamp);
  const std::string end = TimeUtils::formatDate(endTimestamp);
  return (start.empty() ? "?" : start) + " - " + (end.empty() ? "?" : end);
}

uint32_t getCompletionDateForDisplay(const ReadingBookStats& book) {
  return book.completedAt;
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const std::string truncatedValue =
      renderer.truncatedText(UI_12_FONT_ID, value.c_str(), rect.width - 24, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, rect.x + 12, rect.y + METRIC_CARD_VALUE_Y, truncatedValue.c_str(), true,
                    EpdFontFamily::BOLD);

  const std::string truncatedLabel =
      renderer.truncatedText(UI_10_FONT_ID, label, rect.width - 24, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, rect.x + 12, rect.y + METRIC_CARD_LABEL_Y, truncatedLabel.c_str());
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
  resolvedCoverBmpPath.clear();
  coverLoadPending = false;
  if (const auto* book = findBook(bookPath)) {
    resolvedCoverBmpPath = findFastCoverPath(*book);
    coverLoadPending = resolvedCoverBmpPath.empty();
  }
  requestUpdate();
}

void ReadingStatsDetailActivity::setCurrentBookByIndex(const int index) {
  const auto& books = READING_STATS.getBooks();
  if (index < 0 || index >= static_cast<int>(books.size())) {
    return;
  }

  if (books[index].path == bookPath && !coverLoadPending) {
    return;
  }

  const bool showBookTransition = !bookPath.empty();
  bookPath = books[index].path;
  resolvedCoverBmpPath = findFastCoverPath(books[index]);
  coverLoadPending = resolvedCoverBmpPath.empty();
  if (showBookTransition) {
    requestUpdateAndWait();
  } else {
    requestUpdate();
  }
}

void ReadingStatsDetailActivity::navigateBook(const int direction) {
  const auto& books = READING_STATS.getBooks();
  const int bookCount = static_cast<int>(books.size());
  if (bookCount <= 1) {
    return;
  }

  const int currentIndex = findBookIndex(bookPath);
  if (currentIndex < 0) {
    setCurrentBookByIndex(0);
    return;
  }

  const int nextIndex = (currentIndex + direction + bookCount) % bookCount;
  setCurrentBookByIndex(nextIndex);
}

void ReadingStatsDetailActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  buttonNavigator.onPreviousRelease([&]() {
    navigateBook(-1);
  });
  buttonNavigator.onNextRelease([&]() {
    navigateBook(1);
  });

  if (coverLoadPending) {
    coverLoadPending = false;
    if (const auto* book = findBook(bookPath)) {
      const std::string resolvedCoverPath = ensureCoverPath(*book);
      if (!resolvedCoverPath.empty() && resolvedCoverPath != resolvedCoverBmpPath) {
        resolvedCoverBmpPath = resolvedCoverPath;
        requestUpdate();
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && Storage.exists(bookPath.c_str())) {
    onSelectBook(bookPath);
  }
}

void ReadingStatsDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const auto* book = findBook(bookPath);
  const bool hasBookNavigation = READING_STATS.getBooks().size() > 1;
  const auto& lastSessionSnapshot = READING_STATS.getLastSessionSnapshot();
  const bool showCompletionBanner =
      context.showSessionSummary && lastSessionSnapshot.valid && lastSessionSnapshot.path == bookPath &&
      lastSessionSnapshot.completedThisSession;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

  if (!book) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_READING_STATS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", hasBookNavigation ? tr(STR_DIR_UP) : "",
                                              hasBookNavigation ? tr(STR_DIR_DOWN) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const Rect coverRect{metrics.contentSidePadding, contentTop, COVER_WIDTH, COVER_HEIGHT};
  drawCover(renderer, coverRect, resolvedCoverBmpPath);

  const int textX = coverRect.x + coverRect.width + 16;
  const int textWidth = pageWidth - textX - metrics.contentSidePadding;

  int currentY = contentTop + 6;
  const auto wrappedTitle =
      renderer.wrappedText(UI_12_FONT_ID, getDisplayTitle(*book).c_str(), textWidth, 2, EpdFontFamily::BOLD);
  for (const auto& line : wrappedTitle) {
    renderer.drawText(UI_12_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
    currentY += renderer.getLineHeight(UI_12_FONT_ID);
  }

  if (!book->author.empty()) {
    renderer.drawText(UI_10_FONT_ID, textX, currentY + 4, book->author.c_str());
    currentY += renderer.getLineHeight(UI_10_FONT_ID) + 10;
  } else {
    currentY += 10;
  }

  currentY += 6;

  drawProgressBlock(renderer, Rect{textX, currentY, textWidth, PROGRESS_BLOCK_HEIGHT}, tr(STR_BOOK_PROGRESS),
                    book->lastProgressPercent);
  currentY += PROGRESS_BLOCK_HEIGHT + 14;
  drawProgressBlock(renderer, Rect{textX, currentY, textWidth, PROGRESS_BLOCK_HEIGHT}, tr(STR_CHAPTER_PROGRESS),
                    book->chapterProgressPercent);
  currentY += PROGRESS_BLOCK_HEIGHT + 14;

  renderer.drawText(UI_10_FONT_ID, textX, currentY, tr(STR_CURRENT_CHAPTER));
  currentY += renderer.getLineHeight(UI_10_FONT_ID) + 6;

  const std::string currentChapter =
      book->chapterTitle.empty() ? std::string(tr(STR_NOT_SET)) : book->chapterTitle;
  const auto chapterLines = renderer.wrappedText(UI_10_FONT_ID, currentChapter.c_str(), textWidth, 2, EpdFontFamily::BOLD);
  for (const auto& line : chapterLines) {
    renderer.drawText(UI_10_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
    currentY += renderer.getLineHeight(UI_10_FONT_ID);
  }

  int cardsTop = std::max(coverRect.y + coverRect.height, currentY) + metrics.verticalSpacing + 10;
  if (showCompletionBanner) {
    drawSummaryBanner(renderer,
                      Rect{metrics.contentSidePadding, cardsTop, pageWidth - metrics.contentSidePadding * 2,
                           SUMMARY_BANNER_HEIGHT},
                      tr(STR_BOOK_FINISHED), tr(STR_COMPLETED_THIS_SESSION), true);
    cardsTop += SUMMARY_BANNER_HEIGHT + SUMMARY_BANNER_GAP;
  }

  const int cardWidth = (pageWidth - metrics.contentSidePadding * 2 - METRIC_CARD_GAP) / 2;

  drawMetricCard(renderer, Rect{metrics.contentSidePadding, cardsTop, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_LAST_SESSION),
                 formatDurationHm(book->lastSessionMs));
  drawMetricCard(renderer, Rect{metrics.contentSidePadding + cardWidth + METRIC_CARD_GAP, cardsTop, cardWidth,
                                METRIC_CARD_HEIGHT},
                 tr(STR_TOTAL_TIME), formatDurationHm(book->totalReadingMs));
  drawMetricCard(renderer,
                 Rect{metrics.contentSidePadding, cardsTop + METRIC_CARD_HEIGHT + METRIC_CARD_GAP, cardWidth,
                      METRIC_CARD_HEIGHT},
                 tr(STR_SESSIONS), std::to_string(book->sessions));
  drawMetricCard(renderer, Rect{metrics.contentSidePadding + cardWidth + METRIC_CARD_GAP,
                                cardsTop + METRIC_CARD_HEIGHT + METRIC_CARD_GAP, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_STATUS), book->completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS)));
  drawMetricCard(renderer, Rect{metrics.contentSidePadding,
                                cardsTop + (METRIC_CARD_HEIGHT + METRIC_CARD_GAP) * 2,
                                pageWidth - metrics.contentSidePadding * 2, METRIC_CARD_HEIGHT},
                 tr(STR_LAST_READ_DATE), formatDate(book->lastReadAt));
  drawMetricCard(renderer, Rect{metrics.contentSidePadding,
                                cardsTop + (METRIC_CARD_HEIGHT + METRIC_CARD_GAP) * 3,
                                pageWidth - metrics.contentSidePadding * 2, METRIC_CARD_HEIGHT},
                 tr(STR_START_END_DATE), formatDateRange(book->firstReadAt, getCompletionDateForDisplay(*book)));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), Storage.exists(bookPath.c_str()) ? tr(STR_OPEN) : "",
                                            hasBookNavigation ? tr(STR_DIR_UP) : "",
                                            hasBookNavigation ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
