#include "TimeUtils.h"

#include "CrossPointSettings.h"
#include <Arduino.h>
#include <esp_sntp.h>

#include <algorithm>
#include <ctime>

#include "util/TimeZoneRegistry.h"

namespace {
constexpr uint32_t VALID_CLOCK_THRESHOLD = 1704067200UL;  // 2024-01-01 UTC
bool syncedThisBoot = false;
uint8_t configuredTimeZonePreset = UINT8_MAX;

std::string formatDateBuffer(const int year, const unsigned month, const unsigned day, const bool appendBang) {
  const char* bang = appendBang ? "!" : "";
  char buffer[24];

  switch (static_cast<CrossPointSettings::DATE_FORMAT>(SETTINGS.dateFormat)) {
    case CrossPointSettings::DATE_MM_DD_YYYY:
      snprintf(buffer, sizeof(buffer), "%02u/%02u/%04d%s", month, day, year, bang);
      break;
    case CrossPointSettings::DATE_YYYY_MM_DD:
      snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u%s", year, month, day, bang);
      break;
    case CrossPointSettings::DATE_DD_MM_YYYY:
    default:
      snprintf(buffer, sizeof(buffer), "%02u/%02u/%04d%s", day, month, year, bang);
      break;
  }

  return buffer;
}

int32_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

void civilFromDays(int z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  year = static_cast<int>(yearOfEra) + era * 400;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPart = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPart + 2) / 5 + 1;
  month = monthPart + (monthPart < 10 ? 3 : -9);
  year += (month <= 2);
}
}  // namespace

void TimeUtils::configureTimezone() {
  const uint8_t preset = TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset);
  if (configuredTimeZonePreset == preset) {
    return;
  }

  setenv("TZ", TimeZoneRegistry::getPresetPosixTz(preset), 1);
  tzset();
  configuredTimeZonePreset = preset;
}

void TimeUtils::stopNtp() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
}

bool TimeUtils::syncTimeWithNtp(const uint32_t timeoutMs) {
  configureTimezone();
  stopNtp();

  const bool initialClockValid = isClockValid(static_cast<uint32_t>(time(nullptr)));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.nist.gov");
  esp_sntp_init();

  const uint32_t maxRetries = std::max<uint32_t>(1, timeoutMs / 100U);
  for (uint32_t retry = 0; retry < maxRetries; ++retry) {
    const time_t currentTime = time(nullptr);
    const bool currentClockValid = isClockValid(static_cast<uint32_t>(currentTime));
    const bool syncCompleted = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    const bool clockJumpedToValid = !initialClockValid && currentClockValid;

    if ((syncCompleted || clockJumpedToValid) && currentClockValid) {
      syncedThisBoot = true;
      return true;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  return false;
}

bool TimeUtils::isClockValid() { return isClockValid(static_cast<uint32_t>(time(nullptr))); }

bool TimeUtils::isClockValid(const uint32_t epochSeconds) { return epochSeconds >= VALID_CLOCK_THRESHOLD; }

uint32_t TimeUtils::getAuthoritativeTimestamp() {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (syncedThisBoot && isClockValid(now)) {
    return now;
  }
  return 0;
}

uint32_t TimeUtils::getCurrentValidTimestamp() {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  return isClockValid(now) ? now : 0;
}

uint32_t TimeUtils::getLocalDayOrdinal(const uint32_t epochSeconds) {
  configureTimezone();

  if (!isClockValid(epochSeconds)) {
    return 0;
  }

  time_t currentTime = static_cast<time_t>(epochSeconds);
  tm localTime = {};
  if (localtime_r(&currentTime, &localTime) == nullptr) {
    return epochSeconds / 86400UL;
  }

  return static_cast<uint32_t>(
      daysFromCivil(localTime.tm_year + 1900, static_cast<unsigned>(localTime.tm_mon + 1),
                    static_cast<unsigned>(localTime.tm_mday)));
}

uint32_t TimeUtils::getDayOrdinalForDate(const int year, const unsigned month, const unsigned day) {
  return static_cast<uint32_t>(daysFromCivil(year, month, day));
}

bool TimeUtils::getDateFromDayOrdinal(const uint32_t dayOrdinal, int& year, unsigned& month, unsigned& day) {
  civilFromDays(static_cast<int>(dayOrdinal), year, month, day);
  return true;
}

bool TimeUtils::wasTimeSyncedThisBoot() { return syncedThisBoot; }

const char* TimeUtils::getCurrentTimeZoneLabel() {
  return TimeZoneRegistry::getPresetLabel(TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset));
}

std::string TimeUtils::formatDate(const uint32_t epochSeconds, const bool appendBang) {
  if (!isClockValid(epochSeconds)) {
    return "";
  }

  configureTimezone();
  time_t currentTime = static_cast<time_t>(epochSeconds);
  tm localTime = {};
  if (localtime_r(&currentTime, &localTime) == nullptr) {
    return "";
  }

  return formatDateBuffer(localTime.tm_year + 1900, static_cast<unsigned>(localTime.tm_mon + 1),
                          static_cast<unsigned>(localTime.tm_mday), appendBang);
}

std::string TimeUtils::formatDateTime(const uint32_t epochSeconds, const bool appendBang) {
  if (!isClockValid(epochSeconds)) {
    return "";
  }

  configureTimezone();
  time_t currentTime = static_cast<time_t>(epochSeconds);
  tm localTime = {};
  if (localtime_r(&currentTime, &localTime) == nullptr) {
    return "";
  }

  return formatDateBuffer(localTime.tm_year + 1900, static_cast<unsigned>(localTime.tm_mon + 1),
                          static_cast<unsigned>(localTime.tm_mday), appendBang) +
         " " + (localTime.tm_hour < 10 ? "0" : "") + std::to_string(localTime.tm_hour) + ":" +
         (localTime.tm_min < 10 ? "0" : "") + std::to_string(localTime.tm_min);
}

std::string TimeUtils::formatDateParts(const int year, const unsigned month, const unsigned day, const bool appendBang) {
  return formatDateBuffer(year, month, day, appendBang);
}

std::string TimeUtils::formatMonthYear(const int year, const unsigned month) {
  char buffer[16];
  if (static_cast<CrossPointSettings::DATE_FORMAT>(SETTINGS.dateFormat) == CrossPointSettings::DATE_YYYY_MM_DD) {
    snprintf(buffer, sizeof(buffer), "%04d-%02u", year, month);
  } else {
    snprintf(buffer, sizeof(buffer), "%02u/%04d", month, year);
  }
  return buffer;
}
