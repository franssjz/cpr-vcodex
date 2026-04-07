#include "SyncDayActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>

#include "CrossPointState.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int INFO_CARD_HEIGHT = 98;
constexpr int DIAGNOSTICS_CARD_MIN_HEIGHT = 248;
constexpr int CARD_GAP = 16;

void wifiOff() {
  TimeUtils::stopNtp();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void drawInfoCard(GfxRenderer& renderer, const Rect& rect, const char* title, const std::string& primaryLine,
                  const std::string& secondaryLine = "", const std::string& tertiaryLine = "") {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int textWidth = rect.width - 24;
  const int left = rect.x + 12;
  int top = rect.y + 12;

  renderer.drawText(UI_10_FONT_ID, left, top, title, true, EpdFontFamily::BOLD);
  top += 22;

  if (!primaryLine.empty()) {
    const std::string primary =
        renderer.truncatedText(UI_12_FONT_ID, primaryLine.c_str(), textWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, left, top, primary.c_str(), true, EpdFontFamily::BOLD);
    top += 24;
  }

  if (!secondaryLine.empty()) {
    const std::string secondary = renderer.truncatedText(UI_10_FONT_ID, secondaryLine.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, left, top, secondary.c_str());
    top += 18;
  }

  if (!tertiaryLine.empty()) {
    const std::string tertiary = renderer.truncatedText(UI_10_FONT_ID, tertiaryLine.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, left, top, tertiary.c_str());
  }
}

std::string formatTimestampLabel(const uint32_t timestamp, const bool includeTime = false,
                                 const bool appendBang = false) {
  const std::string formatted =
      includeTime ? TimeUtils::formatDateTime(timestamp, appendBang) : TimeUtils::formatDate(timestamp, appendBang);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string getCurrentDateTimeLabel() {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  const std::string formatted = TimeUtils::formatDateTime(now);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string getDeviceTimeLabel() { return formatTimestampLabel(TimeUtils::getAuthoritativeTimestamp(), true); }

std::string getHeaderDateLabel() {
  const auto info = HeaderDateUtils::getDisplayDateInfo();
  if (!TimeUtils::isClockValid(info.timestamp)) {
    return tr(STR_NOT_SET);
  }

  return formatTimestampLabel(info.timestamp, false, info.usedFallback);
}

std::string getFallbackDateLabel() {
  const auto info = HeaderDateUtils::getDisplayDateInfo();
  if (info.usedFallback && TimeUtils::isClockValid(info.timestamp)) {
    return formatTimestampLabel(info.timestamp, true, true);
  }

  return tr(STR_NOT_SET);
}

std::string getBooleanLabel(const bool value) { return value ? tr(STR_YES) : tr(STR_NO); }

std::string getTimeSourceLabel(const bool clockValid, const bool syncedThisBoot,
                               const HeaderDateUtils::DisplayDateInfo& displayInfo) {
  if (clockValid && syncedThisBoot) {
    return tr(STR_TIME_SOURCE_SYNCED);
  }

  if (displayInfo.usedFallback && TimeUtils::isClockValid(displayInfo.timestamp)) {
    return tr(STR_TIME_SOURCE_FALLBACK);
  }

  return tr(STR_TIME_SOURCE_UNAVAILABLE);
}

void drawDiagnosticCard(GfxRenderer& renderer, const Rect& rect) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int textWidth = rect.width - 24;
  const int left = rect.x + 12;
  int top = rect.y + 14;
  const int lineHeight = 18;

  const bool clockValid = TimeUtils::isClockValid();
  const bool syncedThisBoot = TimeUtils::wasTimeSyncedThisBoot();
  const auto displayInfo = HeaderDateUtils::getDisplayDateInfo();

  renderer.drawText(UI_10_FONT_ID, left, top, tr(STR_SYNC_DAY_INFO_TITLE), true, EpdFontFamily::BOLD);
  top += 20;

  const char* infoLines[] = {
      tr(STR_SYNC_DAY_INFO_1),
      tr(STR_SYNC_DAY_INFO_2),
      tr(STR_SYNC_DAY_INFO_3),
  };

  for (const char* infoLine : infoLines) {
    const auto wrappedLines = renderer.wrappedText(UI_10_FONT_ID, infoLine, textWidth, 3);
    for (const auto& wrappedLine : wrappedLines) {
      renderer.drawText(UI_10_FONT_ID, left, top, wrappedLine.c_str());
      top += lineHeight;
    }
    top += 4;
  }

  top += 8;
  renderer.drawLine(left, top, left + textWidth, top);
  top += 16;

  renderer.drawText(UI_10_FONT_ID, left, top, tr(STR_SYNC_DIAGNOSTICS), true, EpdFontFamily::BOLD);
  top += 24;

  const std::string lines[] = {
      std::string(tr(STR_CLOCK_VALID)) + ": " + getBooleanLabel(clockValid),
      std::string(tr(STR_TIME_SOURCE)) + ": " + getTimeSourceLabel(clockValid, syncedThisBoot, displayInfo),
      std::string(tr(STR_CURRENT_CLOCK)) + ": " + getCurrentDateTimeLabel(),
      std::string(tr(STR_SYNCED_THIS_BOOT)) + ": " + getBooleanLabel(syncedThisBoot),
      std::string(tr(STR_HEADER_DATE)) + ": " + getHeaderDateLabel(),
      std::string(tr(STR_FALLBACK_DATE)) + ": " + getFallbackDateLabel(),
  };

  for (const auto& line : lines) {
    const std::string truncated = renderer.truncatedText(UI_10_FONT_ID, line.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, left, top, truncated.c_str());
    top += lineHeight;
  }

  if (displayInfo.usedFallback) {
    top += 2;
    const std::string note = renderer.truncatedText(UI_10_FONT_ID, tr(STR_SYNC_DATE_STALE_NOTE), textWidth);
    renderer.drawText(UI_10_FONT_ID, left, top, note.c_str(), true, EpdFontFamily::BOLD);
    top += lineHeight;
  }
}
}  // namespace

void SyncDayActivity::onEnter() {
  Activity::onEnter();
  TimeUtils::configureTimezone();
  wifiConnectedOnEnter = isWifiConnected();
  connectedInActivity = false;
  syncing = false;
  lastSyncSucceeded = false;
  lastSyncFailed = false;
  lastClockRefreshMs = millis();
  requestUpdate();
}

void SyncDayActivity::onExit() {
  Activity::onExit();

  if (!wifiConnectedOnEnter && connectedInActivity) {
    wifiOff();
  }
}

void SyncDayActivity::loop() {
  updateClockTick();

  if (syncing) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (isWifiConnected()) {
      syncTime();
    } else {
      openWifiSelection();
    }
  }
}

void SyncDayActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SYNC_DAY));

  if (syncing) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_SYNCING_TIME), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 8, tr(STR_SYNC_DAY_HINT));
    renderer.displayBuffer();
    return;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const std::string wifiPrimary =
      isWifiConnected() ? std::string(tr(STR_CONNECTED)) : std::string(tr(STR_NOT_CONNECTED));
  const std::string wifiSecondary = isWifiConnected() ? std::string(tr(STR_NETWORK_PREFIX)) + WiFi.SSID().c_str()
                                                      : std::string(tr(STR_SYNC_DAY_WIFI_HINT));

  drawInfoCard(renderer, Rect{sidePadding, contentTop, pageWidth - sidePadding * 2, INFO_CARD_HEIGHT}, tr(STR_WIFI),
               wifiPrimary, wifiSecondary);

  const int timeCardTop = contentTop + INFO_CARD_HEIGHT + CARD_GAP;
  drawInfoCard(renderer, Rect{sidePadding, timeCardTop, pageWidth - sidePadding * 2, INFO_CARD_HEIGHT},
               tr(STR_DEVICE_TIME), getDeviceTimeLabel(), getStatusMessage());

  const int diagnosticsTop = timeCardTop + INFO_CARD_HEIGHT + CARD_GAP;
  const int diagnosticsHeight = std::max(
      DIAGNOSTICS_CARD_MIN_HEIGHT, pageHeight - diagnosticsTop - metrics.buttonHintsHeight - metrics.verticalSpacing);
  drawDiagnosticCard(renderer, Rect{sidePadding, diagnosticsTop, pageWidth - sidePadding * 2, diagnosticsHeight});

  const char* actionLabel = isWifiConnected() ? tr(STR_SYNC_NOW) : tr(STR_CONNECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), actionLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

bool SyncDayActivity::isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }

std::string SyncDayActivity::getStatusMessage() const {
  if (lastSyncSucceeded) {
    return tr(STR_TIME_SYNCED);
  }
  if (lastSyncFailed) {
    return tr(STR_TIME_SYNC_FAILED);
  }
  if (TimeUtils::isClockValid(TimeUtils::getAuthoritativeTimestamp())) {
    return tr(STR_SYNC_DAY_HINT);
  }
  if (HeaderDateUtils::getDisplayDateInfo().usedFallback) {
    return tr(STR_SYNC_DATE_STALE_NOTE);
  }
  return tr(STR_SYNC_DAY_WIFI_HINT);
}

void SyncDayActivity::openWifiSelection() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || !isWifiConnected()) {
                             requestUpdate();
                             return;
                           }

                           if (!wifiConnectedOnEnter) {
                             connectedInActivity = true;
                           }
                           syncTime();
                         });
}

void SyncDayActivity::syncTime() {
  syncing = true;
  lastSyncSucceeded = false;
  lastSyncFailed = false;
  requestUpdate(true);

  const bool hadValidTimeBefore = TimeUtils::isClockValid();
  const bool ntpSuccess = TimeUtils::syncTimeWithNtp();
  const uint32_t currentValidTimestamp = TimeUtils::getCurrentValidTimestamp();
  const bool effectiveSuccess = ntpSuccess || (!hadValidTimeBefore && currentValidTimestamp > 0);
  if (currentValidTimestamp > 0) {
    APP_STATE.lastKnownValidTimestamp = std::max(APP_STATE.lastKnownValidTimestamp, currentValidTimestamp);
    APP_STATE.saveToFile();
  }
  syncing = false;
  lastSyncSucceeded = effectiveSuccess;
  lastSyncFailed = !effectiveSuccess;
  lastClockRefreshMs = millis();
  requestUpdate(true);
}

void SyncDayActivity::updateClockTick() {
  if (!TimeUtils::isClockValid()) {
    return;
  }

  if (millis() - lastClockRefreshMs < 1000) {
    return;
  }

  lastClockRefreshMs = millis();
  requestUpdate();
}
