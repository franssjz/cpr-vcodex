#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsExtendedActivity.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr int SUMMARY_PANEL_HEIGHT = 222;
constexpr int DETAILS_BUTTON_HEIGHT = 38;
constexpr int LIST_HEADER_HEIGHT = 34;
constexpr int LIST_HEADER_BOTTOM_GAP = 10;
constexpr int BOOK_ROW_HEIGHT = 140;
constexpr int BOOK_ROW_GAP = 12;
constexpr int BOOKS_PER_PAGE = 2;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string getBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return book.completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS));
}

bool isCompletedBook(const ReadingBookStats& book) { return book.completed || book.lastProgressPercent >= 100; }

struct BookSections {
  std::vector<int> started;
  std::vector<int> finished;

  int totalRows() const { return static_cast<int>(started.size() + finished.size()); }
};

BookSections buildBookSections(const std::vector<ReadingBookStats>& books) {
  BookSections sections;
  sections.started.reserve(books.size());
  sections.finished.reserve(books.size());
  for (int index = 0; index < static_cast<int>(books.size()); ++index) {
    if (isCompletedBook(books[index])) {
      sections.finished.push_back(index);
    } else {
      sections.started.push_back(index);
    }
  }
  return sections;
}

const ReadingBookStats* summaryBook(const std::vector<ReadingBookStats>& books, const BookSections& sections) {
  if (!sections.started.empty()) {
    return &books[sections.started.front()];
  }
  return books.empty() ? nullptr : &books.front();
}

const std::vector<int>& activeSection(const BookSections& sections, const int selectedIndex) {
  if (sections.started.empty()) {
    return sections.finished;
  }
  const int flatBookIndex = std::max(0, selectedIndex - 1);
  if (flatBookIndex >= static_cast<int>(sections.started.size())) {
    return sections.finished;
  }
  return sections.started;
}

bool activeSectionIsFinished(const BookSections& sections, const int selectedIndex) {
  if (sections.finished.empty()) {
    return false;
  }
  if (sections.started.empty()) {
    return true;
  }
  return selectedIndex > static_cast<int>(sections.started.size());
}

int selectedLocalIndex(const BookSections& sections, const int selectedIndex) {
  if (selectedIndex <= 0) {
    return 0;
  }
  const int flatBookIndex = selectedIndex - 1;
  if (!sections.started.empty() && flatBookIndex < static_cast<int>(sections.started.size())) {
    return flatBookIndex;
  }
  return std::max(0, flatBookIndex - static_cast<int>(sections.started.size()));
}

int originalBookIndexForSelection(const BookSections& sections, const int selectedIndex) {
  if (selectedIndex <= 0) {
    return -1;
  }
  const int flatBookIndex = selectedIndex - 1;
  if (flatBookIndex < static_cast<int>(sections.started.size())) {
    return sections.started[flatBookIndex];
  }
  const int finishedIndex = flatBookIndex - static_cast<int>(sections.started.size());
  if (finishedIndex >= 0 && finishedIndex < static_cast<int>(sections.finished.size())) {
    return sections.finished[finishedIndex];
  }
  return -1;
}

void drawMiniProgressBar(GfxRenderer& renderer, const Rect& rect, const uint8_t percent);

void drawMoreDetailsButton(GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const char* label = tr(STR_MORE_DETAILS);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2 + 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
}

void drawSummaryLine(GfxRenderer& renderer, const int x, const int y, const int width, const char* label,
                     const std::string& value) {
  renderer.drawText(SMALL_FONT_ID, x + 8, y + 6, label);
  const std::string valueText = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), width - 16, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x + 8, y + 22, valueText.c_str(), true, EpdFontFamily::BOLD);
}

void drawSummaryPanel(GfxRenderer& renderer, const Rect& rect, const uint64_t todayReadingMs,
                      const BookSections& sections) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int pad = 12;

  const auto& books = READING_STATS.getBooks();
  const int heroTop = rect.y + 14;
  const ReadingBookStats* recentBook = summaryBook(books, sections);
  if (recentBook != nullptr) {
    const auto& recent = *recentBook;
    const int progressBoxWidth = 54;
    const int titleWidth = rect.width - pad * 2 - progressBoxWidth - 10;
    const std::string title =
        renderer.truncatedText(UI_12_FONT_ID, getBookTitle(recent).c_str(), titleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + pad, heroTop, title.c_str(), true, EpdFontFamily::BOLD);
    const std::string author =
        renderer.truncatedText(UI_10_FONT_ID, getBookSubtitle(recent).c_str(), titleWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rect.x + pad, heroTop + 23, author.c_str());
    const std::string progressText = std::to_string(recent.lastProgressPercent) + "%";
    const int progressWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + rect.width - pad - progressWidth, heroTop + 5,
                      progressText.c_str(), true, EpdFontFamily::BOLD);
    drawMiniProgressBar(renderer, Rect{rect.x + pad, heroTop + 72, rect.width - pad * 2, 8},
                        recent.lastProgressPercent);
  } else {
    renderer.drawText(UI_10_FONT_ID, rect.x + pad, heroTop + 20, tr(STR_NO_READING_STATS));
  }

  const int statsTop = rect.y + 116;
  const int columnGap = 10;
  const int rowGap = 8;
  const int metricH = 42;
  const int columnWidth = (rect.width - pad * 2 - columnGap) / 2;
  const int secondColumnX = rect.x + pad + columnWidth + columnGap;
  const auto drawMetric = [&](const int x, const int y, const char* label, const std::string& value) {
    drawSummaryLine(renderer, x, y, columnWidth, label, value);
  };
  drawMetric(rect.x + pad, statsTop, tr(STR_TODAY), ReadingStatsAnalytics::formatDurationHm(todayReadingMs));
  drawMetric(secondColumnX, statsTop, tr(STR_READ_STREAK), std::to_string(READING_STATS.getCurrentStreakDays()));
  drawMetric(rect.x + pad, statsTop + metricH + rowGap, tr(STR_TOTAL_TIME),
             ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetric(secondColumnX, statsTop + metricH + rowGap, tr(STR_DONE),
             std::to_string(READING_STATS.getBooksFinishedCount()));
}

void drawMiniProgressBar(GfxRenderer& renderer, const Rect& rect, const uint8_t percent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 4);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4));
  }
}

void drawBookRow(GfxRenderer& renderer, const Rect& rect, const ReadingBookStats& book, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int sidePadding = 12;
  const int topPadding = 12;
  const int innerX = rect.x + sidePadding;
  const int innerY = rect.y + topPadding;
  const int textX = innerX;
  const int textWidth = rect.width - sidePadding * 2;
  const int titleY = innerY;
  const int subtitleY = innerY + 50;
  const int dividerY = rect.y + rect.height - 58;
  const int progressBarY = rect.y + rect.height - 18;

  const std::string progressText = std::to_string(book.lastProgressPercent) + "%";
  const int progressWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int titleWidth = std::max(40, textWidth - progressWidth - 14);
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, getBookTitle(book).c_str(), titleWidth, 2,
                                               EpdFontFamily::BOLD);
  int lineY = titleY;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, textX, lineY, line.c_str(), true, EpdFontFamily::BOLD);
    lineY += renderer.getLineHeight(UI_12_FONT_ID);
  }

  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, getBookSubtitle(book).c_str(), textWidth - 4, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, textX, subtitleY, subtitle.c_str());

  const std::string totalTimeText = ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs);
  renderer.drawText(UI_12_FONT_ID, rect.x + rect.width - sidePadding - progressWidth, titleY + 2,
                    progressText.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawLine(innerX, dividerY, rect.x + rect.width - sidePadding, dividerY);

  const std::string readText = std::string(tr(STR_TOTAL_TIME)) + ": " + totalTimeText;
  const std::string clippedReadText = renderer.truncatedText(UI_10_FONT_ID, readText.c_str(), textWidth,
                                                             EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, textX, dividerY + 8, clippedReadText.c_str(), true, EpdFontFamily::BOLD);

  drawMiniProgressBar(renderer, Rect{innerX, progressBarY, rect.width - sidePadding * 2, 10}, book.lastProgressPercent);
}
}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  const BookSections sections = buildBookSections(READING_STATS.getBooks());
  selectedIndex = sections.totalRows() == 0 ? 0 : 1;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  const BookSections sections = buildBookSections(READING_STATS.getBooks());
  const int selectableCount = sections.totalRows() + 1;
  const int pageItems = BOOKS_PER_PAGE;

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
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedEntry();
    return;
  }

  buttonNavigator.onNextRelease([this, selectableCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, selectableCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, selectableCount, pageItems] {
    if (selectableCount <= 1) {
      return;
    }

    if (selectedIndex == 0) {
      selectedIndex = 1;
    } else {
      const int bookIndex = selectedIndex - 1;
      selectedIndex = ButtonNavigator::nextPageIndex(bookIndex, selectableCount - 1, pageItems) + 1;
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, selectableCount, pageItems] {
    if (selectableCount <= 1) {
      return;
    }

    if (selectedIndex == 0) {
      selectedIndex = ((selectableCount - 2) / pageItems) * pageItems + 1;
    } else {
      const int bookIndex = selectedIndex - 1;
      selectedIndex = ButtonNavigator::previousPageIndex(bookIndex, selectableCount - 1, pageItems) + 1;
    }
    requestUpdate();
  });
}

void ReadingStatsActivity::openSelectedEntry() {
  const auto& books = READING_STATS.getBooks();
  const BookSections sections = buildBookSections(books);
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<ReadingStatsExtendedActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             guardBackReturn();
                             requestUpdate();
                           });
    return;
  }
  const int bookIndex = originalBookIndexForSelection(sections, selectedIndex);
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, books[bookIndex].path),
                         [this](const ActivityResult&) {
                           guardBackReturn();
                           requestUpdate();
                         });
}

void ReadingStatsActivity::guardBackReturn() { waitForBackRelease = true; }

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int detailsTop = summaryTop + SUMMARY_PANEL_HEIGHT + metrics.verticalSpacing;
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const auto& books = READING_STATS.getBooks();
  const BookSections sections = buildBookSections(books);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

  drawSummaryPanel(renderer, Rect{sidePadding, summaryTop, pageWidth - sidePadding * 2, SUMMARY_PANEL_HEIGHT},
                   todayReadingMs, sections);

  drawMoreDetailsButton(renderer, Rect{sidePadding, detailsTop, pageWidth - sidePadding * 2, DETAILS_BUTTON_HEIGHT},
                        selectedIndex == 0);

  const int listHeaderTop = detailsTop + DETAILS_BUTTON_HEIGHT + metrics.verticalSpacing;
  const bool showingFinished = activeSectionIsFinished(sections, selectedIndex);
  const auto& section = activeSection(sections, selectedIndex);
  const int totalPages = std::max(1, static_cast<int>((section.size() + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE));
  const int localSelectedBookIndex = selectedIndex == 0 ? 0 : selectedLocalIndex(sections, selectedIndex);
  const int currentPage = section.empty() || selectedIndex == 0 ? 1 : (localSelectedBookIndex / BOOKS_PER_PAGE) + 1;
  const std::string bookCountLabel = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const char* sectionTitle = showingFinished ? tr(STR_BOOKS_FINISHED) : tr(STR_STARTED_BOOKS);
  const std::string sectionLabel = std::string(sectionTitle) + " (" + std::to_string(section.size()) + ")";
  GUI.drawSubHeader(renderer, Rect{0, listHeaderTop, pageWidth, LIST_HEADER_HEIGHT}, sectionLabel.c_str(),
                    bookCountLabel.c_str());

  const int contentTop = listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;

  if (sections.totalRows() == 0) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop + 20, tr(STR_NO_READING_STATS));
  } else {
    const int pageStartIndex = (localSelectedBookIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    const int pageEndIndex = std::min(static_cast<int>(section.size()), pageStartIndex + BOOKS_PER_PAGE);
    for (int localIndex = pageStartIndex; localIndex < pageEndIndex; ++localIndex) {
      const int rowIndex = localIndex - pageStartIndex;
      const int rowY = contentTop + rowIndex * (BOOK_ROW_HEIGHT + BOOK_ROW_GAP);
      const int originalBookIndex = section[localIndex];
      const int selectedOriginalBookIndex = originalBookIndexForSelection(sections, selectedIndex);
      drawBookRow(renderer, Rect{sidePadding, rowY, pageWidth - sidePadding * 2, BOOK_ROW_HEIGHT},
                  books[originalBookIndex], selectedOriginalBookIndex == originalBookIndex);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
