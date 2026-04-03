
#include "AutoTimeSync.h"

#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Logging.h"
#include "util/TimeUtils.h"

namespace {
constexpr uint32_t IDLE_THRESHOLD_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t RETRY_AFTER_FAILURE_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t MIN_SYNC_INTERVAL_HOURS = 1;
constexpr uint32_t MAX_SYNC_INTERVAL_HOURS = 48;

uint32_t lastInteractionMs = 0;
uint32_t lastFailedAttemptMs = 0;

uint32_t getConfiguredIntervalHours() {
  const uint32_t rawHours = SETTINGS.autoTimeSyncIntervalHours;
  if (rawHours < MIN_SYNC_INTERVAL_HOURS) {
    return MIN_SYNC_INTERVAL_HOURS;
  }
  if (rawHours > MAX_SYNC_INTERVAL_HOURS) {
    return MAX_SYNC_INTERVAL_HOURS;
  }
  return rawHours;
}

bool hasRecentActivity() {
  return lastInteractionMs != 0 && (millis() - lastInteractionMs) <= IDLE_THRESHOLD_MS;
}

bool isSyncDue() {
  if (APP_STATE.lastAutoTimeSync == 0) {
    return true;
  }

  const uint32_t intervalSeconds = getConfiguredIntervalHours() * 3600U;
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (now == 0) {
    return true;
  }
  return (now - APP_STATE.lastAutoTimeSync) >= intervalSeconds;
}

bool canAttemptSync() {
  if (!SETTINGS.autoTimeSyncEnabled) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!hasRecentActivity()) {
    return false;
  }
  if (!isSyncDue()) {
    return false;
  }
  if (lastFailedAttemptMs != 0 && (millis() - lastFailedAttemptMs) < RETRY_AFTER_FAILURE_MS) {
    return false;
  }
  return true;
}
}  // namespace

namespace AutoTimeSync {

static void ntpSyncTask(void* pvParameters);

void noteReaderInteraction(const MappedInputManager& mappedInput) {
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
    lastInteractionMs = millis();
  }
}

void pollReaderSync() {
  if (!canAttemptSync()) {
    return;
  }

  LOG_DBG("ATS", "Reader auto time sync due, starting background NTP sync task");

  // Create background task for NTP sync to avoid blocking the UI
  // This prevents 5-second freezes during reading sessions
  xTaskCreate(&ntpSyncTask, "ntp_sync", 4096, nullptr, 1, nullptr);
}

/**
 * @brief Background task function for NTP synchronization
 * @param pvParameters Unused parameter (required by FreeRTOS)
 *
 * This task runs NTP sync in the background to avoid blocking the reader UI.
 * The 5-second NTP timeout would otherwise cause unacceptable lag during reading.
 */
static void ntpSyncTask(void* pvParameters) {
  (void)pvParameters; // Unused parameter

  LOG_DBG("ATS", "Background NTP sync task started");
  const bool synced = TimeUtils::syncTimeWithNtp(5000);
  TimeUtils::stopNtp();

  if (!synced) {
    LOG_ERR("ATS", "Reader auto time sync failed");
    lastFailedAttemptMs = millis();
  } else {
    const uint32_t currentValidTimestamp = TimeUtils::getCurrentValidTimestamp();
    if (currentValidTimestamp > 0) {
      APP_STATE.lastAutoTimeSync = currentValidTimestamp;
      APP_STATE.lastKnownValidTimestamp = std::max(APP_STATE.lastKnownValidTimestamp, currentValidTimestamp);
      APP_STATE.saveToFile();
      LOG_DBG("ATS", "Reader auto time sync succeeded");
    }
  }

  // Clean up the task
  vTaskDelete(nullptr);
}

}  // namespace AutoTimeSync
