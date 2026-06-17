#include "BookStatsActionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <variant>

#include "AchievementsStore.h"
#include "BookReadingAdjustmentActivity.h"
#include "ReadingDateSelectionActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int ACTION_COUNT = 3;
constexpr int ACTION_ADJUST_READING_TIME = 0;
constexpr int ACTION_MODIFY_START_DATE = 1;
constexpr int ACTION_RESET_BOOK_STATS = 2;

uint32_t getInitialStartDateDayOrdinal(const std::string& bookPath) {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return 0;
  }
  if (TimeUtils::isClockValid(book->firstReadAt)) {
    return TimeUtils::getLocalDayOrdinal(book->firstReadAt);
  }
  return book->readingDays.empty() ? 0 : book->readingDays.front().dayOrdinal;
}
}  // namespace

void BookStatsActionsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = ACTION_ADJUST_READING_TIME;
  startDateApplyFailed = false;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void BookStatsActionsActivity::openAdjustment() {
  startActivityForResult(
      std::make_unique<BookReadingAdjustmentActivity>(renderer, mappedInput, bookPath, bookTitle),
      [this](const ActivityResult&) {
        ActivityResult result;
        setResult(std::move(result));
        finish();
      });
}

void BookStatsActionsActivity::openStartDateSelection() {
  startDateApplyFailed = false;
  startActivityForResult(
      std::make_unique<ReadingDateSelectionActivity>(renderer, mappedInput, getInitialStartDateDayOrdinal(bookPath)),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* page = std::get_if<PageResult>(&result.data);
              page != nullptr && READING_STATS.setBookFirstReadDate(bookPath, page->page)) {
            ActivityResult updatedResult;
            setResult(std::move(updatedResult));
            finish();
            return;
          }
          startDateApplyFailed = true;
        }

        guardConfirmReturn();
        requestUpdate(true);
      });
}

void BookStatsActionsActivity::confirmResetBookStats() {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_THIS_BOOK_STATS_CONFIRM), bookTitle),
      [this](const ActivityResult& result) {
        if (!result.isCancelled && READING_STATS.removeBook(bookPath)) {
          ACHIEVEMENTS.rebuildProgressFromCurrentStats();
          ActivityResult resetResult;
          resetResult.data = MenuResult{RESULT_RESET_BOOK_STATS};
          setResult(std::move(resetResult));
          finish();
          return;
        }

        guardConfirmReturn();
        requestUpdate(true);
      });
}

void BookStatsActionsActivity::guardConfirmReturn() {
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.wasReleased(MappedInputManager::Button::Confirm);
}

void BookStatsActionsActivity::loop() {
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == ACTION_ADJUST_READING_TIME) {
      openAdjustment();
      return;
    }
    if (selectedIndex == ACTION_MODIFY_START_DATE) {
      openStartDateSelection();
      return;
    }
    confirmResetBookStats();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ACTION_COUNT);
    startDateApplyFailed = false;
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ACTION_COUNT);
    startDateApplyFailed = false;
    requestUpdate();
  });
}

void BookStatsActionsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int listHeight = std::min(contentHeight, metrics.listWithSubtitleRowHeight * ACTION_COUNT);
  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), pageWidth - metrics.contentSidePadding * 2);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_BOOK_STATS_ACTIONS), subtitle.c_str());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, listHeight}, ACTION_COUNT, selectedIndex,
      [](const int index) {
        if (index == ACTION_ADJUST_READING_TIME) return std::string(tr(STR_ADJUST_READING_TIME));
        if (index == ACTION_MODIFY_START_DATE) return std::string(tr(STR_MODIFY_START_DATE));
        return std::string(tr(STR_RESET_THIS_BOOK_STATS));
      });

  if (startDateApplyFailed) {
    const int hintTop = contentTop + listHeight + metrics.verticalSpacing;
    const int hintWidth = pageWidth - metrics.contentSidePadding * 2;
    const std::string hint = renderer.truncatedText(UI_10_FONT_ID, tr(STR_CHOOSE_EARLIER_START_DATE), hintWidth);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, hintTop, hint.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
