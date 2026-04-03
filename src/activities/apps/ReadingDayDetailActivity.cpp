#include "ReadingDayDetailActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int SUMMARY_CARD_HEIGHT = 70;
constexpr int SUMMARY_GAP = 8;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int valueFontId =
      renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD) <= rect.width - 24 ? UI_12_FONT_ID
                                                                                                    : UI_10_FONT_ID;
  const std::string truncatedValue =
      renderer.truncatedText(valueFontId, value.c_str(), rect.width - 24, EpdFontFamily::BOLD);
  renderer.drawText(valueFontId, rect.x + 12, rect.y + (valueFontId == UI_12_FONT_ID ? 14 : 18), truncatedValue.c_str(),
                    true, EpdFontFamily::BOLD);

  const auto labelLines =
      renderer.wrappedText(UI_10_FONT_ID, label, rect.width - 24, 2, EpdFontFamily::REGULAR);
  int labelY = rect.y + 40;
  for (const auto& line : labelLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 12, labelY, line.c_str());
    labelY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}
}  // namespace

void ReadingDayDetailActivity::refreshEntries() {
  entries = ReadingStatsAnalytics::getBooksReadOnDay(dayOrdinal);
  if (selectedIndex >= static_cast<int>(entries.size())) {
    selectedIndex = std::max(0, static_cast<int>(entries.size()) - 1);
  }
}

void ReadingDayDetailActivity::openSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()) || entries[selectedIndex].book == nullptr) {
    return;
  }

  startActivityForResult(
      std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, entries[selectedIndex].book->path),
      [this](const ActivityResult&) {
        refreshEntries();
        requestUpdate();
      });
}

void ReadingDayDetailActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void ReadingDayDetailActivity::loop() {
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
    openSelectedBook();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void ReadingDayDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const std::string dateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(dayOrdinal);
  const uint64_t totalReadingMs =
      !entries.empty() ? ReadingStatsAnalytics::buildTimelineDayEntry(dayOrdinal).totalReadingMs : 0;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_DAY), dateLabel.c_str());

  drawMetricCard(renderer, Rect{sidePadding, contentTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_TOTAL_TIME),
                 ReadingStatsAnalytics::formatDurationHm(totalReadingMs));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, contentTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_READ), std::to_string(entries.size()));

  const char* topBookLabel = tr(STR_TOP_BOOK);
  const std::string topBookTitle =
      !entries.empty() && entries.front().book != nullptr ? getBookTitle(*entries.front().book) : std::string(tr(STR_NOT_SET));
  const int listTop = contentTop + SUMMARY_CARD_HEIGHT + metrics.verticalSpacing;
  GUI.drawSubHeader(renderer, Rect{0, listTop, pageWidth, 34}, topBookLabel, topBookTitle.c_str());

  const int listContentTop = listTop + 34 + 10;
  const int listHeight = pageHeight - listContentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, listContentTop + 20, tr(STR_NO_READING_DAY));
  } else {
    GUI.drawList(renderer, Rect{0, listContentTop, pageWidth, listHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) {
                   return entries[index].book ? getBookTitle(*entries[index].book) : std::string(tr(STR_NOT_SET));
                 },
                 [this](const int index) {
                   if (!entries[index].book) {
                     return std::string(tr(STR_NOT_SET));
                   }
                   return entries[index].book->author.empty() ? std::string(tr(STR_IN_PROGRESS)) : entries[index].book->author;
                 },
                 [](const int) { return UIIcon::Book; },
                 [this](const int index) { return ReadingStatsAnalytics::formatDurationHm(entries[index].readingMs); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
