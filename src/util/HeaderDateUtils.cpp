#include "HeaderDateUtils.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>

#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {
void drawHeaderDate(const GfxRenderer& renderer, const ThemeMetrics& metrics, const int pageWidth,
                    const std::string& dateText) {
  if (dateText.empty()) {
    return;
  }

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = pageWidth - 12 - metrics.batteryWidth;
  int rightEdge = batteryX - 8;

  if (showBatteryPercentage) {
    const std::string batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
    rightEdge -= renderer.getTextWidth(SMALL_FONT_ID, batteryText.c_str()) + 4;
  }

  const int dateWidth = renderer.getTextWidth(SMALL_FONT_ID, dateText.c_str());
  const int dateX = std::max(metrics.contentSidePadding, rightEdge - dateWidth);
  renderer.drawText(SMALL_FONT_ID, dateX, metrics.topPadding + 5, dateText.c_str());
}

std::string formatHeaderDateText(const uint32_t timestamp, const bool usedFallback) {
  return TimeUtils::formatDate(timestamp, usedFallback);
}
}  // namespace

HeaderDateUtils::DisplayDateInfo HeaderDateUtils::getDisplayDateInfo() {
  TimeUtils::configureTimezone();
  const uint32_t authoritativeTimestamp = TimeUtils::getAuthoritativeTimestamp();
  if (TimeUtils::isClockValid(authoritativeTimestamp)) {
    return {authoritativeTimestamp, false};
  }

  if (TimeUtils::isClockValid(APP_STATE.lastKnownValidTimestamp)) {
    return {APP_STATE.lastKnownValidTimestamp, true};
  }

  bool usedFallback = false;
  const uint32_t timestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
  return {timestamp, usedFallback};
}

std::string HeaderDateUtils::getDisplayDateText() {
  if (!SETTINGS.displayDay) {
    return "";
  }

  const auto info = getDisplayDateInfo();
  return formatHeaderDateText(info.timestamp, info.usedFallback);
}

void HeaderDateUtils::drawHeaderWithDate(const GfxRenderer& renderer, const char* title, const char* subtitle) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, subtitle);
  drawHeaderDate(renderer, metrics, pageWidth, getDisplayDateText());
}
