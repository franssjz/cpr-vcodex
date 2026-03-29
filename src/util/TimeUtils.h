#pragma once

#include <cstdint>
#include <string>

namespace TimeUtils {

void configureTimezone();
void stopNtp();
bool syncTimeWithNtp(uint32_t timeoutMs = 5000);
bool isClockValid();
bool isClockValid(uint32_t epochSeconds);
uint32_t getAuthoritativeTimestamp();
uint32_t getCurrentValidTimestamp();
uint32_t getLocalDayOrdinal(uint32_t epochSeconds);
uint32_t getDayOrdinalForDate(int year, unsigned month, unsigned day);
bool getDateFromDayOrdinal(uint32_t dayOrdinal, int& year, unsigned& month, unsigned& day);
bool wasTimeSyncedThisBoot();
const char* getCurrentTimeZoneLabel();
std::string formatDate(uint32_t epochSeconds, bool appendBang = false);
std::string formatDateTime(uint32_t epochSeconds, bool appendBang = false);
std::string formatDateParts(int year, unsigned month, unsigned day, bool appendBang = false);
std::string formatMonthYear(int year, unsigned month);

}  // namespace TimeUtils
