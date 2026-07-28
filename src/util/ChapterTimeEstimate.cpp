#include "util/ChapterTimeEstimate.h"

#include <cstdio>

namespace ChapterTimeEstimate {
namespace {
constexpr uint64_t MS_PER_MINUTE = 60ULL * 1000ULL;
constexpr uint64_t MS_PER_HOUR = 60ULL * MS_PER_MINUTE;
constexpr uint64_t MS_PER_DAY = 24ULL * MS_PER_HOUR;
constexpr uint64_t MS_PER_YEAR = 365ULL * MS_PER_DAY;

bool formatRoundedUnit(const uint64_t value, const char unit, char* buf, const size_t bufSize) {
  const int written = snprintf(buf, bufSize, "%llu%c", static_cast<unsigned long long>(value), unit);
  return written > 0 && static_cast<size_t>(written) < bufSize;
}

uint64_t roundedUnits(const uint64_t totalMs, const uint64_t unitMs) {
  uint64_t value = (totalMs + unitMs / 2) / unitMs;
  if (value == 0) {
    value = 1;
  }
  return value;
}
}  // namespace

bool formatCompactDuration(const uint64_t totalMs, char* buf, const size_t bufSize) {
  if (!buf || bufSize < 3 || totalMs == 0) {
    return false;
  }

  // Pick the unit from the rounded display value so 60m becomes 1h (not "60m").
  const uint64_t minutes = roundedUnits(totalMs, MS_PER_MINUTE);
  if (minutes < 60) {
    return formatRoundedUnit(minutes, 'm', buf, bufSize);
  }
  const uint64_t hours = roundedUnits(totalMs, MS_PER_HOUR);
  if (hours < 24) {
    return formatRoundedUnit(hours, 'h', buf, bufSize);
  }
  const uint64_t days = roundedUnits(totalMs, MS_PER_DAY);
  if (days < 365) {
    return formatRoundedUnit(days, 'd', buf, bufSize);
  }
  return formatRoundedUnit(roundedUnits(totalMs, MS_PER_YEAR), 'y', buf, bufSize);
}

bool formatRemainingFromRate(const uint32_t remainingWords, const double wordsPerMs, char* buf,
                             const size_t bufSize) {
  if (remainingWords == 0 || wordsPerMs <= 0.0) {
    return false;
  }
  const double ms = static_cast<double>(remainingWords) / wordsPerMs;
  if (ms <= 0.0 || ms >= static_cast<double>(UINT64_MAX)) {
    return false;
  }
  return formatCompactDuration(static_cast<uint64_t>(ms), buf, bufSize);
}

}  // namespace ChapterTimeEstimate
