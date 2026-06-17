#include "HeaderDateUtils.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {
void drawHeaderTopLine(const GfxRenderer& renderer, const ThemeMetrics& metrics, const int pageWidth,
                       const std::string& dateText, const std::string& reminderText) {
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = pageWidth - 12 - metrics.batteryWidth;
  int rightEdge = batteryX - 8;

  if (showBatteryPercentage) {
    const std::string batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
    rightEdge -= renderer.getTextWidth(SMALL_FONT_ID, batteryText.c_str()) + 4;
  }

  int dateX = rightEdge;
  if (!dateText.empty()) {
    const int dateWidth = renderer.getTextWidth(SMALL_FONT_ID, dateText.c_str());
    dateX = std::max(metrics.contentSidePadding, rightEdge - dateWidth);
    renderer.drawText(SMALL_FONT_ID, dateX, metrics.topPadding + 5, dateText.c_str());
  }

  if (!reminderText.empty()) {
    const int reminderX = metrics.contentSidePadding;
    const int maxReminderWidth = std::max(0, dateX - reminderX - 12);
    if (maxReminderWidth > 0) {
      const std::string truncated = renderer.truncatedText(SMALL_FONT_ID, reminderText.c_str(), maxReminderWidth);
      renderer.drawText(SMALL_FONT_ID, reminderX, metrics.topPadding + 5, truncated.c_str());
    }
  }
}

std::string formatHeaderDateText(const uint32_t timestamp, const bool usedFallback) {
  (void)usedFallback;
  return TimeUtils::formatDate(timestamp, false);
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

std::string HeaderDateUtils::getSyncDayReminderText() {
  const uint8_t threshold = SETTINGS.getSyncDayReminderStartThreshold();
  return APP_STATE.shouldShowSyncDayReminder(threshold) ? std::string(tr(STR_SYNC_DAY_REMINDER_MESSAGE)) : "";
}

void HeaderDateUtils::drawTopLine(GfxRenderer& renderer, const std::string& dateText) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  drawHeaderTopLine(renderer, metrics, pageWidth, dateText, getSyncDayReminderText());
}

void HeaderDateUtils::drawHeaderWithDate(GfxRenderer& renderer, const char* title, const char* subtitle) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, subtitle);
  drawHeaderTopLine(renderer, metrics, pageWidth, getDisplayDateText(), getSyncDayReminderText());
}
