#include "util/ChapterTimeEstimate.h"

#include <cstdio>

namespace ChapterTimeEstimate {
namespace {
constexpr uint64_t MS_PER_MINUTE = 60ULL * 1000ULL;
constexpr uint64_t MS_PER_HOUR = 60ULL * MS_PER_MINUTE;
constexpr uint64_t MS_PER_DAY = 24ULL * MS_PER_HOUR;
constexpr uint64_t MS_PER_YEAR = 365ULL * MS_PER_DAY;
}  // namespace

bool formatCompactDuration(const uint64_t totalMs, char* buf, const size_t bufSize) {
  if (!buf || bufSize < 3 || totalMs == 0) {
    return false;
  }

  if (totalMs < MS_PER_HOUR) {
    uint64_t minutes = (totalMs + MS_PER_MINUTE / 2) / MS_PER_MINUTE;
    if (minutes == 0) {
      minutes = 1;
    }
    return snprintf(buf, bufSize, "%llum", static_cast<unsigned long long>(minutes)) > 0;
  }
  if (totalMs < MS_PER_DAY) {
    uint64_t hours = (totalMs + MS_PER_HOUR / 2) / MS_PER_HOUR;
    if (hours == 0) {
      hours = 1;
    }
    return snprintf(buf, bufSize, "%lluh", static_cast<unsigned long long>(hours)) > 0;
  }
  if (totalMs < MS_PER_YEAR) {
    uint64_t days = (totalMs + MS_PER_DAY / 2) / MS_PER_DAY;
    if (days == 0) {
      days = 1;
    }
    return snprintf(buf, bufSize, "%llud", static_cast<unsigned long long>(days)) > 0;
  }

  uint64_t years = (totalMs + MS_PER_YEAR / 2) / MS_PER_YEAR;
  if (years == 0) {
    years = 1;
  }
  return snprintf(buf, bufSize, "%lluy", static_cast<unsigned long long>(years)) > 0;
}

uint64_t estimateRemainingMs(const uint32_t remainingWords, const double wordsPerMs) {
  if (remainingWords == 0 || wordsPerMs <= 0.0) {
    return 0;
  }
  const double ms = static_cast<double>(remainingWords) / wordsPerMs;
  if (ms <= 0.0 || ms >= static_cast<double>(UINT64_MAX)) {
    return 0;
  }
  return static_cast<uint64_t>(ms);
}

}  // namespace ChapterTimeEstimate
