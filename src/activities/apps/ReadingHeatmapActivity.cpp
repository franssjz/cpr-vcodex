#include "ReadingHeatmapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <string>

#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "ReadingDayDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int SECTION_GAP = 10;
constexpr int MONTH_HEADER_HEIGHT = 34;
constexpr int SUMMARY_CARD_HEIGHT = 66;
constexpr int SUMMARY_CARD_GAP = 8;
constexpr int HEATMAP_GRID_GAP = 6;
constexpr int LEGEND_HEIGHT = 30;
constexpr int LEGEND_SWATCH_SIZE = 16;

struct HeatmapCell {
  uint32_t dayOrdinal = 0;
  uint64_t readingMs = 0;
  unsigned day = 0;
  bool inViewedMonth = false;
  bool isReferenceDay = false;
  bool isSelected = false;
};

struct MonthSummary {
  uint64_t totalReadingMs = 0;
  uint64_t bestDayReadingMs = 0;
  uint32_t daysRead = 0;
  unsigned bestDayOfMonth = 0;
};

bool isLeapYear(const int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

unsigned getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29U : 28U;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return DAYS_PER_MONTH[month - 1];
}

unsigned clampDayToMonth(const int year, const unsigned month, const unsigned preferredDay) {
  const unsigned daysInMonth = getDaysInMonth(year, month);
  if (preferredDay == 0) {
    return 1;
  }
  return std::min(preferredDay, daysInMonth);
}

uint32_t getReferenceDisplayTimestamp() {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (TimeUtils::isClockValid(now)) {
    return now;
  }

  if (TimeUtils::isClockValid(APP_STATE.lastKnownValidTimestamp)) {
    return APP_STATE.lastKnownValidTimestamp;
  }

  return READING_STATS.getDisplayTimestamp();
}

void resolveReferenceMonth(int& year, unsigned& month, uint32_t& dayOrdinal) {
  const uint32_t referenceTimestamp = getReferenceDisplayTimestamp();
  if (TimeUtils::isClockValid(referenceTimestamp)) {
    dayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  } else if (READING_STATS.hasReadingDays()) {
    dayOrdinal = READING_STATS.getReadingDays().back().dayOrdinal;
  } else {
    dayOrdinal = 0;
  }

  unsigned day = 0;
  if (dayOrdinal == 0 || !TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    year = 2026;
    month = 1;
    dayOrdinal = 0;
  }
}

std::string formatMonthLabel(const int year, const unsigned month) {
  return ReadingStatsAnalytics::formatMonthLabel(year, month);
}

int getHeatLevel(const uint64_t readingMs) {
  if (readingMs == 0) {
    return 0;
  }

  const uint64_t totalMinutes = std::max<uint64_t>(1, readingMs / 60000ULL);
  if (totalMinutes < 10ULL) {
    return 1;
  }
  if (totalMinutes < 30ULL) {
    return 2;
  }
  if (totalMinutes < 60ULL) {
    return 3;
  }
  return 4;
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int valueFontId =
      renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD) <= rect.width - 20 ? UI_12_FONT_ID
                                                                                                    : UI_10_FONT_ID;
  const std::string truncatedValue =
      renderer.truncatedText(valueFontId, value.c_str(), rect.width - 20, EpdFontFamily::BOLD);
  renderer.drawText(valueFontId, rect.x + 10, rect.y + (valueFontId == UI_12_FONT_ID ? 13 : 16), truncatedValue.c_str(),
                    true, EpdFontFamily::BOLD);

  const auto labelLines =
      renderer.wrappedText(UI_10_FONT_ID, label, rect.width - 20, 2, EpdFontFamily::REGULAR);
  int labelY = rect.y + 39;
  for (const auto& line : labelLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 10, labelY, line.c_str());
    labelY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void drawGoalCheckBadge(GfxRenderer& renderer, const Rect& rect, const bool darkBackground) {
  constexpr int checkWidth = 20;
  constexpr int checkHeight = 16;
  constexpr int paddingRight = 7;
  constexpr int paddingBottom = 7;

  const int checkX = rect.x + rect.width - checkWidth - paddingRight;
  const int checkY = rect.y + rect.height - checkHeight - paddingBottom;
  const bool checkColor = darkBackground ? false : true;

  renderer.drawLine(checkX, checkY + 8, checkX + 5, checkY + 13, 4, checkColor);
  renderer.drawLine(checkX + 5, checkY + 13, checkX + 17, checkY + 1, 4, checkColor);
}

void drawHeatCell(GfxRenderer& renderer, const Rect& rect, const HeatmapCell& cell) {
  const int level = cell.inViewedMonth ? getHeatLevel(cell.readingMs) : 0;
  const Rect fillRect{rect.x + 1, rect.y + 1, std::max(0, rect.width - 2), std::max(0, rect.height - 2)};
  bool textBlack = true;

  switch (level) {
    case 1:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::LightGray);
      break;
    case 2:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::DarkGray);
      break;
    case 3:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::DarkGray);
      renderer.drawRect(fillRect.x + 2, fillRect.y + 2, std::max(0, fillRect.width - 4), std::max(0, fillRect.height - 4));
      break;
    case 4:
      renderer.fillRect(fillRect.x, fillRect.y, fillRect.width, fillRect.height);
      textBlack = false;
      break;
    default:
      break;
  }

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const std::string dayText = cell.day == 0 ? "" : std::to_string(cell.day);
  const int dayTextX = rect.x + 6;
  const int dayTextY = rect.y + 5;
  if (!dayText.empty()) {
    renderer.drawText(SMALL_FONT_ID, dayTextX, dayTextY, dayText.c_str(), textBlack,
                      cell.inViewedMonth ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  if (cell.inViewedMonth && cell.readingMs >= getDailyReadingGoalMs()) {
    drawGoalCheckBadge(renderer, rect, level == 4);
  }

  if (cell.isReferenceDay) {
    renderer.drawRect(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4, level == 4 ? false : true);
  }
  if (cell.isSelected) {
    renderer.drawRect(rect.x + 4, rect.y + 4, std::max(0, rect.width - 8), std::max(0, rect.height - 8),
                      level == 4 ? false : true);
  }
}

void drawLegendSwatch(GfxRenderer& renderer, const Rect& rect, const int level) {
  const Rect heatRect{rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2};

  switch (level) {
    case 1:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::LightGray);
      break;
    case 2:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::DarkGray);
      break;
    case 3:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::DarkGray);
      renderer.drawRect(heatRect.x + 2, heatRect.y + 2, std::max(0, heatRect.width - 4), std::max(0, heatRect.height - 4));
      break;
    case 4:
      renderer.fillRect(heatRect.x, heatRect.y, heatRect.width, heatRect.height);
      break;
    default:
      break;
  }

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
}

MonthSummary buildMonthSummary(const int year, const unsigned month) {
  MonthSummary summary;
  const uint32_t monthStart = TimeUtils::getDayOrdinalForDate(year, month, 1);
  const uint32_t monthEnd = TimeUtils::getDayOrdinalForDate(year, month, getDaysInMonth(year, month));
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.dayOrdinal < monthStart || day.dayOrdinal > monthEnd) {
      continue;
    }

    summary.totalReadingMs += day.readingMs;
    if (day.readingMs > 0) {
      summary.daysRead++;
    }
    if (day.readingMs > summary.bestDayReadingMs) {
      int dayYear = 0;
      unsigned dayMonth = 0;
      unsigned dayOfMonth = 0;
      TimeUtils::getDateFromDayOrdinal(day.dayOrdinal, dayYear, dayMonth, dayOfMonth);
      summary.bestDayReadingMs = day.readingMs;
      summary.bestDayOfMonth = dayOfMonth;
    }
  }
  return summary;
}

std::array<HeatmapCell, 42> buildHeatmapCells(const int year, const unsigned month, const uint32_t referenceDayOrdinal,
                                              const uint32_t selectedDayOrdinal) {
  std::array<HeatmapCell, 42> cells{};
  const uint32_t firstDayOrdinal = TimeUtils::getDayOrdinalForDate(year, month, 1);
  const unsigned daysInMonth = getDaysInMonth(year, month);
  const int firstWeekday = static_cast<int>((firstDayOrdinal + 3U) % 7U);  // Monday = 0
  const uint32_t gridStartOrdinal = firstDayOrdinal - static_cast<uint32_t>(firstWeekday);

  for (size_t index = 0; index < cells.size(); ++index) {
    auto& cell = cells[index];
    cell.dayOrdinal = gridStartOrdinal + static_cast<uint32_t>(index);
    int cellYear = 0;
    unsigned cellMonth = 0;
    unsigned cellDay = 0;
    TimeUtils::getDateFromDayOrdinal(cell.dayOrdinal, cellYear, cellMonth, cellDay);
    cell.day = cellDay;
    cell.inViewedMonth = cellYear == year && cellMonth == month;
    cell.isReferenceDay = cell.inViewedMonth && referenceDayOrdinal != 0 && cell.dayOrdinal == referenceDayOrdinal;
    cell.isSelected = cell.inViewedMonth && selectedDayOrdinal != 0 && cell.dayOrdinal == selectedDayOrdinal;
  }

  size_t readingIndex = 0;
  const auto& readingDays = READING_STATS.getReadingDays();
  for (auto& cell : cells) {
    while (readingIndex < readingDays.size() && readingDays[readingIndex].dayOrdinal < cell.dayOrdinal) {
      readingIndex++;
    }
    if (readingIndex < readingDays.size() && readingDays[readingIndex].dayOrdinal == cell.dayOrdinal) {
      cell.readingMs = readingDays[readingIndex].readingMs;
    }
  }

  return cells;
}

void drawLegend(GfxRenderer& renderer, const Rect& rect) {
  struct LegendLevel {
    int level;
    const char* label;
  };
  static constexpr LegendLevel LEVELS[] = {{1, "1m+"}, {2, "10m+"}, {3, "30m+"}, {4, "60m+"}};

  const int itemWidth = rect.width / 4;
  for (int index = 0; index < 4; ++index) {
    const int itemX = rect.x + index * itemWidth;
    const Rect swatch{itemX + 6, rect.y + 3, LEGEND_SWATCH_SIZE, LEGEND_SWATCH_SIZE};
    drawLegendSwatch(renderer, swatch, LEVELS[index].level);
    const int labelX = itemX + 28;
    renderer.drawText(SMALL_FONT_ID, labelX, rect.y + 6, LEVELS[index].label);
  }
}
}  // namespace

void ReadingHeatmapActivity::onEnter() {
  Activity::onEnter();

  uint32_t referenceDayOrdinal = 0;
  resolveReferenceMonth(viewedYear, viewedMonth, referenceDayOrdinal);
  if (viewedMonth == 0) {
    viewedYear = 2026;
    viewedMonth = 1;
  }

  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  resetSelectedDay();
  requestUpdate();
}

void ReadingHeatmapActivity::goToAdjacentMonth(const int delta) {
  int currentYear = 0;
  unsigned currentMonth = 0;
  unsigned currentDay = 1;
  if (selectedDayOrdinal != 0) {
    TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, currentYear, currentMonth, currentDay);
  }

  int month = static_cast<int>(viewedMonth) + delta;
  int year = viewedYear;
  while (month < 1) {
    month += 12;
    year--;
  }
  while (month > 12) {
    month -= 12;
    year++;
  }
  viewedYear = year;
  viewedMonth = static_cast<unsigned>(month);
  const unsigned targetDay = clampDayToMonth(viewedYear, viewedMonth, currentDay);
  selectedDayOrdinal = TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, targetDay);
  requestUpdate();
}

void ReadingHeatmapActivity::goToReferenceMonth() {
  uint32_t referenceDayOrdinal = 0;
  int year = 0;
  unsigned month = 0;
  resolveReferenceMonth(year, month, referenceDayOrdinal);
  if (year == viewedYear && month == viewedMonth) {
    return;
  }
  viewedYear = year;
  viewedMonth = month;
  resetSelectedDay();
  requestUpdate();
}

void ReadingHeatmapActivity::resetSelectedDay() {
  uint32_t referenceDayOrdinal = 0;
  resolveReferenceMonth(viewedYear, viewedMonth, referenceDayOrdinal);

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (referenceDayOrdinal != 0 && TimeUtils::getDateFromDayOrdinal(referenceDayOrdinal, year, month, day) && year == viewedYear &&
      month == viewedMonth) {
    selectedDayOrdinal = referenceDayOrdinal;
    return;
  }

  for (const auto& readingDay : READING_STATS.getReadingDays()) {
    if (readingDay.readingMs == 0) {
      continue;
    }
    if (!TimeUtils::getDateFromDayOrdinal(readingDay.dayOrdinal, year, month, day)) {
      continue;
    }
    if (year == viewedYear && month == viewedMonth) {
      selectedDayOrdinal = readingDay.dayOrdinal;
      return;
    }
  }

  selectedDayOrdinal = TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, 1);
}

void ReadingHeatmapActivity::moveSelection(const int delta) {
  if (selectedDayOrdinal == 0) {
    resetSelectedDay();
    requestUpdate();
    return;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, year, month, day) || year != viewedYear || month != viewedMonth) {
    resetSelectedDay();
    requestUpdate();
    return;
  }

  const uint32_t firstDayOrdinal = TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, 1);
  const uint32_t lastDayOrdinal =
      TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, getDaysInMonth(viewedYear, viewedMonth));
  int32_t nextOrdinal = static_cast<int32_t>(selectedDayOrdinal) + delta;
  if (nextOrdinal < 1) {
    nextOrdinal = 1;
  }

  if (nextOrdinal < static_cast<int32_t>(firstDayOrdinal) || nextOrdinal > static_cast<int32_t>(lastDayOrdinal)) {
    int nextYear = 0;
    unsigned nextMonth = 0;
    unsigned nextDay = 0;
    if (TimeUtils::getDateFromDayOrdinal(static_cast<uint32_t>(nextOrdinal), nextYear, nextMonth, nextDay)) {
      viewedYear = nextYear;
      viewedMonth = nextMonth;
    }
  }

  selectedDayOrdinal = static_cast<uint32_t>(nextOrdinal);
  requestUpdate();
}

void ReadingHeatmapActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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
    startActivityForResult(std::make_unique<ReadingDayDetailActivity>(renderer, mappedInput, selectedDayOrdinal),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { moveSelection(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { moveSelection(1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { goToAdjacentMonth(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { goToAdjacentMonth(1); });
}

void ReadingHeatmapActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_HEATMAP));

  const uint32_t referenceTimestamp = getReferenceDisplayTimestamp();
  const uint32_t referenceDayOrdinal = TimeUtils::isClockValid(referenceTimestamp)
                                           ? TimeUtils::getLocalDayOrdinal(referenceTimestamp)
                                           : (READING_STATS.hasReadingDays() ? READING_STATS.getReadingDays().back().dayOrdinal : 0);
  const auto monthSummary = buildMonthSummary(viewedYear, viewedMonth);
  const auto cells = buildHeatmapCells(viewedYear, viewedMonth, referenceDayOrdinal, selectedDayOrdinal);
  const std::string monthLabel = formatMonthLabel(viewedYear, viewedMonth);
  const std::string selectedDateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(selectedDayOrdinal);

  GUI.drawSubHeader(renderer, Rect{0, contentTop, pageWidth, MONTH_HEADER_HEIGHT}, monthLabel.c_str(),
                    selectedDateLabel.empty() ? nullptr : selectedDateLabel.c_str());

  const int summaryTop = contentTop + MONTH_HEADER_HEIGHT + 4;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_CARD_GAP) / 2;
  const std::string bestDayValue =
      monthSummary.bestDayOfMonth > 0
          ? ReadingStatsAnalytics::formatDurationHm(monthSummary.bestDayReadingMs) + " (" +
                std::to_string(monthSummary.bestDayOfMonth) + ")"
          : ReadingStatsAnalytics::formatDurationHm(monthSummary.bestDayReadingMs);
  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_MONTH_TOTAL),
                 ReadingStatsAnalytics::formatDurationHm(monthSummary.totalReadingMs));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_CARD_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_DAYS_READ), std::to_string(monthSummary.daysRead));
  drawMetricCard(renderer, Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_CARD_GAP, cardWidth,
                                SUMMARY_CARD_HEIGHT},
                 tr(STR_BEST_DAY), bestDayValue);
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_CARD_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_CARD_GAP,
                      cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_STREAK), std::to_string(READING_STATS.getCurrentStreakDays()));

  const int gridTop = summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_CARD_GAP) * 2 + SECTION_GAP;
  const int legendTop = pageHeight - metrics.buttonHintsHeight - LEGEND_HEIGHT - 4;
  const int gridHeight = std::max(120, legendTop - gridTop - SECTION_GAP);
  const int cellWidth = (pageWidth - sidePadding * 2 - HEATMAP_GRID_GAP * 6) / 7;
  const int cellHeight = (gridHeight - HEATMAP_GRID_GAP * 5) / 6;

  for (int index = 0; index < 42; ++index) {
    const int row = index / 7;
    const int col = index % 7;
    const int x = sidePadding + col * (cellWidth + HEATMAP_GRID_GAP);
    const int y = gridTop + row * (cellHeight + HEATMAP_GRID_GAP);
    drawHeatCell(renderer, Rect{x, y, cellWidth, cellHeight}, cells[static_cast<size_t>(index)]);
  }

  drawLegend(renderer, Rect{sidePadding, legendTop, pageWidth - sidePadding * 2, LEGEND_HEIGHT});

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
