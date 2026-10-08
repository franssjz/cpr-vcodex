#include "HeaderDateUtils.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <components/controls/header.h>

#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderStatusLayout.h"
#include "util/TimeUtils.h"

namespace {
void drawHeaderTopLine(const GfxRenderer& renderer, const ThemeMetrics& metrics, const int pageWidth,
                       const std::string& dateText, const std::string& reminderText) {
  namespace fui = freeink::ui;
  fui::HeaderProps props;
  BaseTheme::applyHeaderStatus(renderer, props);
  const auto& status = props.status;
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int batteryWidth =
      status.showBattery
          ? status.battery.glyphWidth + 2 +
                (status.battery.label ? status.battery.gap + renderer.getTextWidth(SMALL_FONT_ID, status.battery.label)
                                      : 0)
          : 0;
  const int clockWidth = status.clockText ? renderer.getTextWidth(SMALL_FONT_ID, status.clockText) : 0;
  const int dateWidth = renderer.getTextWidth(SMALL_FONT_ID, dateText.c_str());
  const auto available =
      headerstatus::freeInterval(left, pageWidth - right, std::max<int>(metrics.contentSidePadding, status.edgeInset),
                                 batteryWidth, status.batteryLeft, clockWidth, status.clockCentered, dateWidth);
  if (available.width() == 0) return;
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_LABEL, SMALL_FONT_ID);
  const auto drawText = [&](int x, const std::string& text) {
    const auto ink = target.inkBounds(fui::GfxRendererTarget::FONT_LABEL, text.c_str(), status.battery.text);
    const int y = metrics.topPadding + (status.stripHeight - ink.height) / 2 - ink.y;
    renderer.drawText(SMALL_FONT_ID, x, y, text.c_str());
  };

  int dateX = available.right;
  if (!dateText.empty()) {
    if (dateWidth <= available.width()) {
      dateX -= dateWidth;
      drawText(dateX, dateText);
    } else {
      const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, dateText.c_str(), available.width());
      dateX -= renderer.getTextWidth(SMALL_FONT_ID, fitted.c_str());
      drawText(dateX, fitted);
    }
  }

  if (!reminderText.empty()) {
    const int reminderX = available.left;
    const int maxReminderWidth = std::max(0, dateX - reminderX - 12);
    if (maxReminderWidth > 0) {
      const std::string truncated = renderer.truncatedText(SMALL_FONT_ID, reminderText.c_str(), maxReminderWidth);
      drawText(reminderX, truncated);
    }
  }
}

std::string formatHeaderDateText(const uint32_t timestamp, const bool usedFallback) {
  (void)usedFallback;
  return TimeUtils::formatDate(timestamp, false);
}

std::string formatHeaderTimeText(const uint32_t timestamp) {
  if (!TimeUtils::isClockValid(timestamp)) {
    return "";
  }

  TimeUtils::configureTimezone();
  time_t currentTime = static_cast<time_t>(timestamp);
  tm localTime = {};
  if (localtime_r(&currentTime, &localTime) == nullptr) {
    return "";
  }

  const bool pm = localTime.tm_hour >= 12;
  int hour12 = localTime.tm_hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%d:%02d%s", hour12, localTime.tm_min, pm ? "PM" : "AM");
  return buffer;
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
  if (!SETTINGS.shouldShowHeaderDate() && !SETTINGS.shouldShowHeaderTime()) {
    return "";
  }

  std::string text;
  if (SETTINGS.shouldShowHeaderDate()) {
    const auto info = getDisplayDateInfo();
    text = formatHeaderDateText(info.timestamp, info.usedFallback);
  }

  if (SETTINGS.shouldShowHeaderTime()) {
    const uint32_t timeTimestamp = TimeUtils::getAuthoritativeTimestamp();
    const std::string timeText = formatHeaderTimeText(timeTimestamp);
    if (!timeText.empty()) {
      if (!text.empty()) {
        text += " ";
      }
      text += timeText;
    }
  }

  return text;
}

std::string HeaderDateUtils::getSyncDayReminderText() {
  const uint8_t threshold = SETTINGS.getEffectiveSyncDayReminderStartThreshold();
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
