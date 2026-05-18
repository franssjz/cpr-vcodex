#include "ReaderBookInfoActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "BookMetadataStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookIdentity.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
std::string basenameTitle(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name = name.substr(0, dot);
  }
  return name;
}

void mergeIfEmpty(std::string& target, const std::string& source) {
  if (target.empty() && !source.empty()) {
    target = source;
  }
}

std::string valueOrDash(const std::string& value) { return value.empty() ? "-" : value; }

void drawWrapped(GfxRenderer& renderer, const int fontId, const int x, int& y, const int width,
                 const std::string& text, const int maxLines,
                 const EpdFontFamily::Style family = EpdFontFamily::REGULAR) {
  const auto lines = renderer.wrappedText(fontId, text.c_str(), width, maxLines, family);
  for (const auto& line : lines) {
    renderer.drawText(fontId, x, y, line.c_str(), true, family);
    y += renderer.getLineHeight(fontId);
  }
}
}  // namespace

ReaderBookInfoActivity::ReaderBookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string path, std::string title, std::string author,
                                               std::string language, std::string currentChapter)
    : Activity("ReaderBookInfo", renderer, mappedInput),
      path(std::move(path)),
      title(std::move(title)),
      author(std::move(author)),
      language(std::move(language)),
      currentChapter(std::move(currentChapter)) {}

void ReaderBookInfoActivity::loadInfo() {
  info = {};
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string bookId =
      !normalizedPath.empty() && Storage.exists(normalizedPath.c_str()) ? BookIdentity::resolveStableBookId(normalizedPath) : "";

  CachedBookMetadata local;
  local.title = title;
  local.author = author;
  local.language = language;
  BOOK_METADATA.importSidecarForBook(normalizedPath, local);

  const CachedBookMetadata* cached = BOOK_METADATA.findBook(normalizedPath, bookId);
  if (cached != nullptr) {
    info.title = cached->title;
    info.author = cached->author;
    info.series = cached->series;
    info.seriesIndex = cached->seriesIndex;
    info.tags = cached->tags;
    info.publisher = cached->publisher;
    info.language = cached->language;
    info.description = cached->description;
    info.identifier = cached->identifier;
  }

  mergeIfEmpty(info.title, title);
  mergeIfEmpty(info.author, author);
  mergeIfEmpty(info.language, language);
  if (info.title.empty()) {
    info.title = basenameTitle(normalizedPath);
  }

  const ReadingBookStats* stats = READING_STATS.findMatchingBookForPath(normalizedPath, info.title, info.author);
  if (stats != nullptr) {
    mergeIfEmpty(info.author, stats->author);
    info.progress = std::to_string(stats->lastProgressPercent) + "%";
    info.totalRead = ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs);
    info.currentChapter = !currentChapter.empty() ? currentChapter : stats->chapterTitle;
    if (stats->lastReadAt > 0) {
      info.lastOpened = TimeUtils::formatDate(stats->lastReadAt);
    }
    const auto estimate = ReadingStatsAnalytics::buildBookTimeLeftEstimate(*stats);
    info.timeLeft = ReadingStatsAnalytics::formatTimeLeftEstimate(estimate);
  } else {
    info.currentChapter = currentChapter;
  }
}

void ReaderBookInfoActivity::fetchMetadata() {
  CachedBookMetadata local;
  local.title = title;
  local.author = author;
  local.language = language;
  const bool imported = BOOK_METADATA.importSidecarForBook(path, local);
  loadInfo();
  statusMessage = imported ? tr(STR_METADATA_IMPORT_OK) : tr(STR_METADATA_IMPORT_MISSING);
  requestUpdate();
}

void ReaderBookInfoActivity::onEnter() {
  Activity::onEnter();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  loadInfo();
  requestUpdate();
}

void ReaderBookInfoActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    fetchMetadata();
    return;
  }

  buttonNavigator.onNext([this] {
    if (maxScrollOffset > 0) {
      scrollOffset = std::min(maxScrollOffset, scrollOffset + 34);
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this] {
    if (scrollOffset > 0) {
      scrollOffset = std::max(0, scrollOffset - 34);
      requestUpdate();
    }
  });
}

void ReaderBookInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int contentWidth = pageWidth - sidePadding * 2;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + 10, tr(STR_BOOK_INFO), true, EpdFontFamily::BOLD);

  int y = contentTop - scrollOffset;
  const auto drawCard = [&](const int height, const auto& content) {
    renderer.drawRect(sidePadding, y, contentWidth, height);
    int cardY = y + 9;
    content(sidePadding + 10, cardY, contentWidth - 20);
    y += height + 10;
  };

  drawCard(92, [&](const int x, int& cardY, const int width) {
    renderer.drawText(SMALL_FONT_ID, x, cardY, tr(STR_CURRENT_BOOK), true, EpdFontFamily::BOLD);
    cardY += renderer.getLineHeight(SMALL_FONT_ID) + 4;
    drawWrapped(renderer, UI_12_FONT_ID, x, cardY, width, valueOrDash(info.title), 2, EpdFontFamily::BOLD);
    if (!info.author.empty()) {
      cardY += 2;
      drawWrapped(renderer, UI_10_FONT_ID, x, cardY, width, info.author, 1);
    }
    if (!info.series.empty()) {
      cardY += 2;
      const std::string seriesLine = info.seriesIndex.empty() ? info.series : info.series + " #" + info.seriesIndex;
      drawWrapped(renderer, SMALL_FONT_ID, x, cardY, width, seriesLine, 1);
    }
  });

  drawCard(112, [&](const int x, int& cardY, const int width) {
    renderer.drawText(UI_10_FONT_ID, x, cardY, tr(STR_READING), true, EpdFontFamily::BOLD);
    cardY += renderer.getLineHeight(UI_10_FONT_ID) + 6;
    const int colWidth = (width - 8) / 2;
    const auto drawMetric = [&](const int col, const int row, const char* label, const std::string& value) {
      const int mx = x + col * (colWidth + 8);
      const int my = cardY + row * 32;
      renderer.drawText(SMALL_FONT_ID, mx, my, label, true, EpdFontFamily::BOLD);
      const std::string safe = renderer.truncatedText(SMALL_FONT_ID, valueOrDash(value).c_str(), colWidth);
      renderer.drawText(SMALL_FONT_ID, mx, my + 13, safe.c_str());
    };
    drawMetric(0, 0, tr(STR_CURRENT_PROGRESS), info.progress);
    drawMetric(1, 0, tr(STR_TOTAL_READ), info.totalRead);
    drawMetric(0, 1, tr(STR_TIME_LEFT), info.timeLeft);
    drawMetric(1, 1, tr(STR_LAST_OPENED), info.lastOpened);
  });

  if (!info.currentChapter.empty()) {
    drawCard(54, [&](const int x, int& cardY, const int width) {
      renderer.drawText(SMALL_FONT_ID, x, cardY, tr(STR_CURRENT_CHAPTER), true, EpdFontFamily::BOLD);
      cardY += renderer.getLineHeight(SMALL_FONT_ID) + 4;
      drawWrapped(renderer, SMALL_FONT_ID, x, cardY, width, info.currentChapter, 2);
    });
  }

  const bool hasMetadata = !info.series.empty() || !info.seriesIndex.empty() || !info.tags.empty() ||
                           !info.publisher.empty() || !info.language.empty() || !info.identifier.empty();
  if (hasMetadata) {
    int rowCount = 0;
    rowCount += !info.series.empty();
    rowCount += !info.seriesIndex.empty();
    rowCount += !info.tags.empty();
    rowCount += !info.publisher.empty();
    rowCount += !info.language.empty();
    rowCount += !info.identifier.empty();
    const int detailHeight = 34 + rowCount * (renderer.getLineHeight(SMALL_FONT_ID) + 5);
    drawCard(detailHeight, [&](const int x, int& cardY, const int width) {
      renderer.drawText(UI_10_FONT_ID, x, cardY, tr(STR_DETAILS), true, EpdFontFamily::BOLD);
      cardY += renderer.getLineHeight(UI_10_FONT_ID) + 6;
      const int labelWidth = 74;
      const auto drawRow = [&](const char* label, const std::string& value) {
        if (value.empty()) return;
        renderer.drawText(SMALL_FONT_ID, x, cardY, label, true, EpdFontFamily::BOLD);
        const std::string safe = renderer.truncatedText(SMALL_FONT_ID, value.c_str(), width - labelWidth - 6);
        renderer.drawText(SMALL_FONT_ID, x + labelWidth, cardY, safe.c_str());
        cardY += renderer.getLineHeight(SMALL_FONT_ID) + 5;
      };
      drawRow(tr(STR_SERIES), info.series);
      drawRow(tr(STR_SERIES_INDEX), info.seriesIndex);
      drawRow(tr(STR_TAGS), info.tags);
      drawRow(tr(STR_PUBLISHER), info.publisher);
      drawRow(tr(STR_LANGUAGE), info.language);
      drawRow(tr(STR_IDENTIFIER), info.identifier);
    });
  }

  if (!info.description.empty()) {
    drawCard(132, [&](const int x, int& cardY, const int width) {
      renderer.drawText(UI_10_FONT_ID, x, cardY, tr(STR_DESCRIPTION), true, EpdFontFamily::BOLD);
      cardY += renderer.getLineHeight(UI_10_FONT_ID) + 6;
      drawWrapped(renderer, SMALL_FONT_ID, x, cardY, width, info.description, 6);
    });
  }

  if (!statusMessage.empty()) {
    drawCard(52, [&](const int x, int& cardY, const int width) {
      drawWrapped(renderer, SMALL_FONT_ID, x, cardY, width, statusMessage, 2);
    });
  }

  maxScrollOffset = std::max(0, y + scrollOffset - viewportBottom);
  scrollOffset = std::min(scrollOffset, maxScrollOffset);

  renderer.fillRect(0, 0, pageWidth, contentTop, false);
  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + 10, tr(STR_BOOK_INFO), true, EpdFontFamily::BOLD);
  if (viewportBottom < pageHeight) {
    renderer.fillRect(0, viewportBottom, pageWidth, pageHeight - viewportBottom, false);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_IMPORT_METADATA), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
