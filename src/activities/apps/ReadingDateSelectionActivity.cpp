#include "ReadingDateSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int FIELD_COUNT = 3;
constexpr int MIN_DAY = 1;
constexpr int MIN_MONTH = 1;
constexpr int MAX_MONTH = 12;
constexpr int MIN_YEAR = 2024;
constexpr int MAX_YEAR = 2099;

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

unsigned getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29U : 28U;
  }
  if (month < 1 || month > 12) {
    return 31;
  }
  return DAYS_PER_MONTH[month - 1];
}

std::string formatTwoDigits(const unsigned value) {
  char buffer[4];
  snprintf(buffer, sizeof(buffer), "%02u", value);
  return buffer;
}

unsigned wrapValue(const unsigned value, const int delta, const unsigned minValue, const unsigned maxValue) {
  const int range = static_cast<int>(maxValue - minValue + 1);
  int offset = static_cast<int>(value - minValue) + delta;
  offset %= range;
  if (offset < 0) {
    offset += range;
  }
  return minValue + static_cast<unsigned>(offset);
}
}  // namespace

void ReadingDateSelectionActivity::onEnter() {
  Activity::onEnter();

  bool initialized = false;
  if (initialDayOrdinal != 0) {
    initialized = TimeUtils::getDateFromDayOrdinal(initialDayOrdinal, year, month, day);
  }

  if (!initialized) {
    bool usedFallback = false;
    const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
    if (TimeUtils::isClockValid(referenceTimestamp)) {
      time_t currentTime = static_cast<time_t>(referenceTimestamp);
      tm localTime = {};
      if (localtime_r(&currentTime, &localTime) != nullptr) {
        year = std::clamp(localTime.tm_year + 1900, MIN_YEAR, MAX_YEAR);
        month = static_cast<unsigned>(std::clamp(localTime.tm_mon + 1, MIN_MONTH, MAX_MONTH));
        day = static_cast<unsigned>(
            std::clamp(localTime.tm_mday, MIN_DAY, static_cast<int>(getDaysInMonth(year, month))));
      }
    }
  }

  selectedField = 0;
  requestUpdate();
}

void ReadingDateSelectionActivity::adjustSelectedField(const int delta) {
  if (selectedField == 0) {
    day = wrapValue(day, delta, MIN_DAY, getDaysInMonth(year, month));
  } else if (selectedField == 1) {
    month = wrapValue(month, delta, MIN_MONTH, MAX_MONTH);
    day = std::min(day, getDaysInMonth(year, month));
  } else {
    year = std::clamp(year + delta, MIN_YEAR, MAX_YEAR);
    day = std::min(day, getDaysInMonth(year, month));
  }
  requestUpdate();
}

uint32_t ReadingDateSelectionActivity::getSelectedDayOrdinal() const {
  return TimeUtils::getDayOrdinalForDate(year, month, day);
}

std::string ReadingDateSelectionActivity::getSelectedDateLabel() const {
  return TimeUtils::formatDateParts(year, month, day);
}

void ReadingDateSelectionActivity::finishWithDate() {
  setResult(ActivityResult{PageResult{getSelectedDayOrdinal()}});
  finish();
}

void ReadingDateSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finishWithDate();
    return;
  }

  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this] {
    selectedField = ButtonNavigator::nextIndex(selectedField, FIELD_COUNT);
    requestUpdate();
  });

  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this] {
    selectedField = ButtonNavigator::previousIndex(selectedField, FIELD_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustSelectedField(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustSelectedField(1); });
}

void ReadingDateSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = metrics.listWithSubtitleRowHeight * FIELD_COUNT;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SET_DATE), getSelectedDateLabel().c_str());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, listHeight}, FIELD_COUNT, selectedField,
      [](int index) {
        if (index == 0) return std::string(tr(STR_DAY));
        if (index == 1) return std::string(tr(STR_MONTH));
        return std::string(tr(STR_YEAR));
      },
      [this](int index) {
        if (index == 0) return formatTwoDigits(day);
        if (index == 1) return formatTwoDigits(month);
        return std::to_string(year);
      },
      [](int) { return UIIcon::Recent; }, nullptr, false);

  const int hintTop = contentTop + listHeight + metrics.verticalSpacing;
  const int hintWidth = pageWidth - sidePadding * 2;
  const std::string hint = renderer.truncatedText(UI_10_FONT_ID, tr(STR_SET_DATE_HINT), hintWidth);
  renderer.drawText(UI_10_FONT_ID, sidePadding, hintTop, hint.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
