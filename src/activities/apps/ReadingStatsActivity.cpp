#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "AppMetricCard.h"
#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsExtendedActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr unsigned long DELETE_STATS_HOLD_MS = 1000;
constexpr int SUMMARY_CARD_HEIGHT = 76;
constexpr int SUMMARY_GAP = 10;
constexpr int DETAILS_BUTTON_HEIGHT = 58;
constexpr int LIST_HEADER_HEIGHT = 34;
constexpr int LIST_HEADER_BOTTOM_GAP = 10;
constexpr int BOOK_ROW_HEIGHT = 80;
constexpr int BOOK_ROW_GAP = 10;
constexpr int BOOKS_PER_PAGE = 3;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string getBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return book.completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS));
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

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
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  } else {
    renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);
  }

  const int sidePadding = 12;
  const int topPadding = 9;
  const int metaWidth = 88;
  const int innerX = rect.x + sidePadding;
  const int innerY = rect.y + topPadding;
  const int textWidth = rect.width - sidePadding * 2 - metaWidth;
  const int titleY = innerY;
  const int subtitleY = innerY + 26;
  const int progressBarY = rect.y + rect.height - 14;

  const std::string title = renderer.truncatedText(UI_12_FONT_ID, getBookTitle(book).c_str(), textWidth - 4,
                                                   EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, innerX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, getBookSubtitle(book).c_str(), textWidth - 4, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, innerX, subtitleY, subtitle.c_str());

  const std::string progressText = std::to_string(book.lastProgressPercent) + "%";
  const std::string totalTimeText = ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs);
  const int progressWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int timeWidth = renderer.getTextWidth(UI_10_FONT_ID, totalTimeText.c_str());
  const int progressX = rect.x + rect.width - sidePadding - progressWidth;
  const int timeX = rect.x + rect.width - sidePadding - timeWidth;

  renderer.drawText(UI_12_FONT_ID, progressX, titleY, progressText.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, timeX, subtitleY, totalTimeText.c_str());

  drawMiniProgressBar(renderer, Rect{innerX, progressBarY, rect.width - sidePadding * 2, 9},
                      book.lastProgressPercent);
}
}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = READING_STATS.getBooks().empty() ? 0 : 1;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
  const int selectableCount = bookCount + 1;
  const int pageItems = BOOKS_PER_PAGE;

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
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<ReadingStatsExtendedActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }

  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  const auto& selectedBook = books[bookIndex];
  if (mappedInput.getHeldTime() >= DELETE_STATS_HOLD_MS) {
    const std::string selectedPath = selectedBook.path;
    auto handler = [this, selectedPath](const ActivityResult& result) {
      if (!result.isCancelled) {
        READING_STATS.removeBook(selectedPath);
        const int remainingBooks = static_cast<int>(READING_STATS.getBooks().size());
        if (selectedIndex > remainingBooks) {
          selectedIndex = remainingBooks > 0 ? remainingBooks : 0;
        }
      }
      requestUpdate();
    };

    startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_STATS_ENTRY),
                                                                  getBookTitle(selectedBook)),
                           handler);
    return;
  }

  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, selectedBook.path),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int detailsTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string dailyGoalValue =
      ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
      ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_STREAK),
                 std::to_string(READING_STATS.getCurrentStreakDays()));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_MAX_STREAK), std::to_string(READING_STATS.getMaxStreakDays()));
  drawMetricCard(renderer, Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth,
                                SUMMARY_CARD_HEIGHT},
                 tr(STR_DAILY_GOAL), dailyGoalValue, todayReadingMs >= getDailyReadingGoalMs());
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth,
                      SUMMARY_CARD_HEIGHT},
                 tr(STR_READING_TIME), ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetricCard(renderer, Rect{sidePadding, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2, cardWidth,
                                SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_FINISHED), std::to_string(READING_STATS.getBooksFinishedCount()));
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2,
                      cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_STARTED), std::to_string(READING_STATS.getBooksStartedCount()));

  drawMoreDetailsButton(renderer, Rect{sidePadding, detailsTop, pageWidth - sidePadding * 2, DETAILS_BUTTON_HEIGHT},
                        selectedIndex == 0);

  const int listHeaderTop = detailsTop + DETAILS_BUTTON_HEIGHT + metrics.verticalSpacing;
  const auto& books = READING_STATS.getBooks();
  const int totalPages = std::max(1, static_cast<int>((books.size() + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE));
  const int currentPage = books.empty() || selectedIndex == 0 ? 1 : ((selectedIndex - 1) / BOOKS_PER_PAGE) + 1;
  const std::string bookCountLabel = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const std::string startedBooksLabel =
      std::string(tr(STR_STARTED_BOOKS)) + " (" + std::to_string(READING_STATS.getBooksStartedCount()) + ")";
  GUI.drawSubHeader(renderer, Rect{0, listHeaderTop, pageWidth, LIST_HEADER_HEIGHT}, startedBooksLabel.c_str(),
                    bookCountLabel.c_str());

  const int contentTop = listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop + 20, tr(STR_NO_READING_STATS));
  } else {
    const int selectedBookIndex = std::max(0, selectedIndex - 1);
    const int pageStartIndex = (selectedBookIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    const int pageEndIndex = std::min(static_cast<int>(books.size()), pageStartIndex + BOOKS_PER_PAGE);
    for (int index = pageStartIndex; index < pageEndIndex; ++index) {
      const int rowIndex = index - pageStartIndex;
      const int rowY = contentTop + rowIndex * (BOOK_ROW_HEIGHT + BOOK_ROW_GAP);
      drawBookRow(renderer, Rect{sidePadding, rowY, pageWidth - sidePadding * 2, BOOK_ROW_HEIGHT}, books[index],
                  selectedIndex == index + 1);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
