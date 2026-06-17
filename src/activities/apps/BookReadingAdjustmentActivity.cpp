#include "BookReadingAdjustmentActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "AchievementsStore.h"
#include "ReadingDateSelectionActivity.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int FIELD_COUNT = 3;
constexpr int OPERATION_COUNT = 2;
constexpr int DURATION_COUNT = 4;
constexpr int64_t MINUTES_TO_MS = 60LL * 1000LL;
constexpr int DURATION_MINUTES[DURATION_COUNT] = {15, 30, 45, 60};

int wrapIndex(const int value, const int delta, const int count) {
  int next = value + delta;
  next %= count;
  if (next < 0) {
    next += count;
  }
  return next;
}
}  // namespace

void BookReadingAdjustmentActivity::onEnter() {
  Activity::onEnter();
  selectedField = 0;
  selectedOperation = 0;
  selectedDuration = 1;
  lastApplyFailed = false;
  initializeSelectedDate();
  requestUpdate();
}

void BookReadingAdjustmentActivity::adjustSelectedValue(const int delta) {
  lastApplyFailed = false;
  if (selectedField == 0) {
    selectedOperation = wrapIndex(selectedOperation, delta, OPERATION_COUNT);
  } else if (selectedField == 1) {
    if (selectedDayOrdinal != 0 || delta > 0) {
      selectedDayOrdinal = static_cast<uint32_t>(static_cast<int32_t>(selectedDayOrdinal) + delta);
    }
  } else {
    selectedDuration = wrapIndex(selectedDuration, delta, DURATION_COUNT);
  }
  requestUpdate();
}

void BookReadingAdjustmentActivity::initializeSelectedDate() {
  bool usedFallback = false;
  const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
  if (!TimeUtils::isClockValid(referenceTimestamp)) {
    selectedDayOrdinal = 0;
    return;
  }

  selectedDayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
}

void BookReadingAdjustmentActivity::openDateSelection() {
  startActivityForResult(std::make_unique<ReadingDateSelectionActivity>(renderer, mappedInput, selectedDayOrdinal),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             if (const auto* page = std::get_if<PageResult>(&result.data)) {
                               selectedDayOrdinal = page->page;
                               lastApplyFailed = false;
                             }
                           }
                           requestUpdate();
                         });
}

int32_t BookReadingAdjustmentActivity::getSelectedDeltaMs() const {
  int32_t deltaMs = static_cast<int32_t>(DURATION_MINUTES[selectedDuration] * MINUTES_TO_MS);
  if (selectedOperation == 1) {
    deltaMs = -deltaMs;
  }
  return deltaMs;
}

uint64_t BookReadingAdjustmentActivity::getSelectedDayReadingMs() const {
  if (selectedDayOrdinal == 0) {
    return 0;
  }

  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return 0;
  }

  for (const auto& day : book->readingDays) {
    if (day.dayOrdinal == selectedDayOrdinal) {
      return day.readingMs;
    }
    if (day.dayOrdinal > selectedDayOrdinal) {
      break;
    }
  }
  return 0;
}

bool BookReadingAdjustmentActivity::canApplySelectedAdjustment() const {
  if (selectedDayOrdinal == 0 || READING_STATS.findBook(bookPath) == nullptr) {
    return false;
  }

  const int32_t deltaMs = getSelectedDeltaMs();
  if (deltaMs >= 0) {
    return true;
  }

  return getSelectedDayReadingMs() >= static_cast<uint64_t>(-deltaMs);
}

std::string BookReadingAdjustmentActivity::getAdjustmentPreviewInfo() const {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return tr(STR_BOOK_NOT_FOUND);
  }
  if (selectedDayOrdinal == 0) {
    return tr(STR_SET_DATE_BEFORE_APPLYING);
  }

  const uint64_t currentMs = getSelectedDayReadingMs();
  const int32_t deltaMs = getSelectedDeltaMs();
  const std::string dayTotal = std::string(tr(STR_DAY_TOTAL)) + ": ";
  if (deltaMs < 0) {
    const uint64_t removeMs = static_cast<uint64_t>(-deltaMs);
    if (currentMs < removeMs) {
      return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " (" + tr(STR_NOT_ENOUGH) + ")";
    }
    return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
           ReadingStatsAnalytics::formatDurationHm(currentMs - removeMs);
  }

  return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
         ReadingStatsAnalytics::formatDurationHm(currentMs + static_cast<uint64_t>(deltaMs));
}

const char* BookReadingAdjustmentActivity::getOperationLabel() const {
  return selectedOperation == 0 ? tr(STR_ADD) : tr(STR_SUBTRACT);
}

std::string BookReadingAdjustmentActivity::getDateLabel() const {
  int selectedYear = 0;
  unsigned selectedMonth = 0;
  unsigned selectedDay = 0;
  if (selectedDayOrdinal == 0 ||
      !TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, selectedYear, selectedMonth, selectedDay)) {
    return tr(STR_NOT_SET);
  }
  return TimeUtils::formatDateParts(selectedYear, selectedMonth, selectedDay);
}

const char* BookReadingAdjustmentActivity::getDurationLabel() const {
  static char label[12];
  snprintf(label, sizeof(label), "%d min", DURATION_MINUTES[selectedDuration]);
  return label;
}

bool BookReadingAdjustmentActivity::applyAdjustment() {
  const uint32_t dayOrdinal = selectedDayOrdinal;
  const int32_t deltaMs = getSelectedDeltaMs();

  if (!READING_STATS.adjustBookReadingTime(bookPath, dayOrdinal, deltaMs)) {
    lastApplyFailed = true;
    requestUpdate();
    return false;
  }

  ACHIEVEMENTS.rebuildProgressFromCurrentStats();
  finish();
  return true;
}

void BookReadingAdjustmentActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedField == 1) {
      openDateSelection();
      return;
    }
    applyAdjustment();
    return;
  }

  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this] {
    selectedField = ButtonNavigator::nextIndex(selectedField, FIELD_COUNT);
    lastApplyFailed = false;
    requestUpdate();
  });

  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this] {
    selectedField = ButtonNavigator::previousIndex(selectedField, FIELD_COUNT);
    lastApplyFailed = false;
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustSelectedValue(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustSelectedValue(1); });
}

void BookReadingAdjustmentActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = metrics.listWithSubtitleRowHeight * FIELD_COUNT;
  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), pageWidth - metrics.contentSidePadding * 2);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ADJUST_READING_TIME), subtitle.c_str());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, listHeight}, FIELD_COUNT, selectedField,
      [](int index) {
        if (index == 0) return std::string(tr(STR_ACTION));
        if (index == 1) return std::string(tr(STR_DATE));
        return std::string(tr(STR_AMOUNT));
      },
      [this](int index) {
        if (index == 0) return std::string(getOperationLabel());
        if (index == 1) return getDateLabel();
        return std::string(getDurationLabel());
      },
      [](int index) { return index == 0 ? UIIcon::Settings : UIIcon::Recent; }, nullptr, false);

  const int infoTop = contentTop + listHeight + metrics.verticalSpacing;
  const int infoWidth = pageWidth - sidePadding * 2;
  std::string info = getAdjustmentPreviewInfo();
  std::string hint = selectedField == 1 ? tr(STR_SELECT_OPENS_DATE_PICKER) : tr(STR_SELECT_APPLIES_CORRECTION);
  if (lastApplyFailed) {
    hint = tr(STR_COULD_NOT_APPLY_CORRECTION);
  } else if (!canApplySelectedAdjustment()) {
    hint = tr(STR_CHOOSE_ADD_OR_REDUCE_AMOUNT);
  }
  const std::string shortInfo = renderer.truncatedText(UI_10_FONT_ID, info.c_str(), infoWidth);
  renderer.drawText(UI_10_FONT_ID, sidePadding, infoTop, shortInfo.c_str());
  const std::string shortHint = renderer.truncatedText(UI_10_FONT_ID, hint.c_str(), infoWidth);
  renderer.drawText(UI_10_FONT_ID, sidePadding, infoTop + renderer.getLineHeight(UI_10_FONT_ID), shortHint.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), selectedField == 1 ? tr(STR_SELECT) : tr(STR_CONFIRM),
                                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
