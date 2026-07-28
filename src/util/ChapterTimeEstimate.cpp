#include "util/ChapterTimeEstimate.h"

#include <cstdio>

namespace ChapterTimeEstimate {
namespace {
constexpr uint64_t MS_PER_MINUTE = 60ULL * 1000ULL;
constexpr uint64_t MS_PER_HOUR = 60ULL * MS_PER_MINUTE;
constexpr uint64_t MS_PER_DAY = 24ULL * MS_PER_HOUR;
constexpr uint64_t MS_PER_YEAR = 365ULL * MS_PER_DAY;

bool formatRoundedUnit(const uint64_t totalMs, const uint64_t unitMs, const char unit, char* buf,
                       const size_t bufSize) {
  uint64_t value = (totalMs + unitMs / 2) / unitMs;
  if (value == 0) {
    value = 1;
  }
  return snprintf(buf, bufSize, "%llu%c", static_cast<unsigned long long>(value), unit) > 0;
}
}  // namespace

bool formatCompactDuration(const uint64_t totalMs, char* buf, const size_t bufSize) {
  if (!buf || bufSize < 3 || totalMs == 0) {
    return false;
  }
  if (totalMs < MS_PER_HOUR) {
    return formatRoundedUnit(totalMs, MS_PER_MINUTE, 'm', buf, bufSize);
  }
  if (totalMs < MS_PER_DAY) {
    return formatRoundedUnit(totalMs, MS_PER_HOUR, 'h', buf, bufSize);
  }
  if (totalMs < MS_PER_YEAR) {
    return formatRoundedUnit(totalMs, MS_PER_DAY, 'd', buf, bufSize);
  }
  return formatRoundedUnit(totalMs, MS_PER_YEAR, 'y', buf, bufSize);
}

}  // namespace ChapterTimeEstimate
