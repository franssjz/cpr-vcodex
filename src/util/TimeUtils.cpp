#include "TimeUtils.h"

#include <Arduino.h>
#include <HalClock.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include <algorithm>
#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "util/TimeZoneRegistry.h"
#include "util/Timezones.h"

namespace {
constexpr uint32_t VALID_CLOCK_THRESHOLD = 1704067200UL;  // 2024-01-01 UTC
bool syncedThisBoot = false;
int lastBridgedRtcUtcHour = -1;

bool writeRtcFromUtcEpoch(const uint32_t epochSeconds) {
  if (!halClock.isAvailable()) {
    return false;
  }
  time_t utcTime = static_cast<time_t>(epochSeconds);
  struct tm utc{};
  gmtime_r(&utcTime, &utc);
  return halClock.writeUtcTm(utc);
}

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

unsigned getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29U : 28U;
  }
  if (month < 1 || month > 12) {
    return 0;
  }
  return DAYS_PER_MONTH[month - 1];
}

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

void TimeUtils::configureTimezone() { timezones::applyToClock(); }

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
      writeRtcFromUtcEpoch(static_cast<uint32_t>(currentTime));
      return true;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  return false;
}

bool TimeUtils::isClockValid() { return isClockValid(static_cast<uint32_t>(time(nullptr))); }

bool TimeUtils::isClockValid(const uint32_t epochSeconds) { return epochSeconds >= VALID_CLOCK_THRESHOLD; }

uint32_t TimeUtils::getAuthoritativeTimestamp() {
  if (isHardwareRtcAutoDayClockActive() && SETTINGS.clockHasBeenSynced && !syncedThisBoot) {
    applySystemClockFromRtc(true);
  }

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

uint32_t TimeUtils::getBestEffortFileTimestamp() {
  // Prefer a live system clock once RTC/NTP has been applied this boot.
  const uint32_t authoritative = getAuthoritativeTimestamp();
  if (isClockValid(authoritative)) {
    return authoritative;
  }

  // Without a live clock, use the last Sync Day / last known good wall time.
  if (isClockValid(APP_STATE.lastKnownValidTimestamp)) {
    return APP_STATE.lastKnownValidTimestamp;
  }

  // Any currently valid system time (e.g. set without syncedThisBoot).
  return getCurrentValidTimestamp();
}

bool TimeUtils::setCurrentDate(const int year, const unsigned month, const unsigned day, uint32_t* epochSeconds) {
  configureTimezone();

  const unsigned daysInMonth = getDaysInMonth(year, month);
  if (day < 1 || daysInMonth == 0 || day > daysInMonth) {
    return false;
  }

  tm localTime = {};
  localTime.tm_year = year - 1900;
  localTime.tm_mon = static_cast<int>(month) - 1;
  localTime.tm_mday = static_cast<int>(day);
  localTime.tm_hour = 12;
  localTime.tm_min = 0;
  localTime.tm_sec = 0;
  localTime.tm_isdst = -1;

  const time_t epoch = mktime(&localTime);
  if (epoch < 0 || !isClockValid(static_cast<uint32_t>(epoch))) {
    return false;
  }

  timeval tv{};
  tv.tv_sec = epoch;
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }

  syncedThisBoot = true;
  if (epochSeconds) {
    *epochSeconds = static_cast<uint32_t>(epoch);
  }
  writeRtcFromUtcEpoch(static_cast<uint32_t>(epoch));
  return true;
}

bool TimeUtils::getTimestampForLocalDate(const int year, const unsigned month, const unsigned day,
                                         uint32_t* epochSeconds) {
  configureTimezone();

  const unsigned daysInMonth = getDaysInMonth(year, month);
  if (day < 1 || daysInMonth == 0 || day > daysInMonth) {
    return false;
  }

  tm localTime = {};
  localTime.tm_year = year - 1900;
  localTime.tm_mon = static_cast<int>(month) - 1;
  localTime.tm_mday = static_cast<int>(day);
  localTime.tm_hour = 12;
  localTime.tm_min = 0;
  localTime.tm_sec = 0;
  localTime.tm_isdst = -1;

  const time_t epoch = mktime(&localTime);
  if (epoch < 0 || !isClockValid(static_cast<uint32_t>(epoch))) {
    return false;
  }

  if (epochSeconds) {
    *epochSeconds = static_cast<uint32_t>(epoch);
  }
  return true;
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

  return static_cast<uint32_t>(daysFromCivil(localTime.tm_year + 1900, static_cast<unsigned>(localTime.tm_mon + 1),
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

const char* TimeUtils::getCurrentTimeZoneLabel() { return timezones::table()[timezones::activeIndex()].name; }

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

std::string TimeUtils::formatDateParts(const int year, const unsigned month, const unsigned day,
                                       const bool appendBang) {
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

bool TimeUtils::isHardwareRtcAutoDayClockActive() { return SETTINGS.isHardwareRtcAutoDayClockActive(); }

bool TimeUtils::formatStatusBarClockTime(char* buf, const size_t bufSize, const bool use12Hour) {
  if (bufSize < (use12Hour ? 9u : 6u) || !halClock.isAvailable()) {
    return false;
  }

  uint32_t epoch = 0;
  if (!halClock.readUtcEpoch(epoch, false)) {
    return false;
  }

  configureTimezone();
  time_t utcTime = static_cast<time_t>(epoch);
  struct tm localTime{};
  if (localtime_r(&utcTime, &localTime) == nullptr) {
    return false;
  }

  if (use12Hour) {
    const bool pm = localTime.tm_hour >= 12;
    int hour12 = localTime.tm_hour % 12;
    if (hour12 == 0) {
      hour12 = 12;
    }
    snprintf(buf, bufSize, "%d:%02d %s", hour12, localTime.tm_min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  }
  return true;
}

bool TimeUtils::applySystemClockFromRtc(const bool forceRefresh) {
  if (!halClock.isAvailable() || !SETTINGS.clockHasBeenSynced) {
    return false;
  }

  struct tm utc{};
  if (!halClock.readUtcTm(utc, forceRefresh)) {
    return false;
  }

  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t epoch = mktime(&utc);
  configureTimezone();
  if (epoch < 0 || !isClockValid(static_cast<uint32_t>(epoch))) {
    return false;
  }

  timeval tv{};
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }

  syncedThisBoot = true;
  lastBridgedRtcUtcHour = utc.tm_hour;
  APP_STATE.registerValidTimeSync(static_cast<uint32_t>(epoch));
  return true;
}

void TimeUtils::tickSystemClockFromRtc() {
  if (!halClock.isAvailable() || !SETTINGS.clockHasBeenSynced) {
    return;
  }

  uint8_t utcHour = 0;
  uint8_t utcMinute = 0;
  if (!halClock.getUtcTime(utcHour, utcMinute, false)) {
    return;
  }

  const int observedHour = static_cast<int>(utcHour);
  if (lastBridgedRtcUtcHour >= 0 && observedHour == lastBridgedRtcUtcHour) {
    return;
  }

  if (!applySystemClockFromRtc(true)) {
    lastBridgedRtcUtcHour = observedHour;
  }
}
