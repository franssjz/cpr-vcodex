#include "ReaderUtils.h"

#include <I18n.h>

#include <cstdio>
#include <cstdint>

namespace ReaderUtils {
namespace {
constexpr uint64_t MS_PER_MINUTE = 60ULL * 1000ULL;

bool formatRoundedUnit(const uint64_t value, const char* unit, char* buf, const size_t bufSize) {
  if (!unit || unit[0] == '\0') {
    return false;
  }
  // ETA unit suffixes are authored in EN+ES only; other locales fall back to
  // English via I18n (intentional — do not invent unit translations everywhere).
  const int written = snprintf(buf, bufSize, "%llu%s", static_cast<unsigned long long>(value), unit);
  return written > 0 && static_cast<size_t>(written) < bufSize;
}
}  // namespace

bool formatCompactDuration(const uint64_t totalMs, char* buf, const size_t bufSize) {
  if (!buf || bufSize < 3 || totalMs == 0) {
    return false;
  }

  // Cascade on rounded smaller units so 60m → 1h and 24h → 1d (never "60m" / "24h").
  uint64_t minutes = (totalMs + MS_PER_MINUTE / 2) / MS_PER_MINUTE;
  if (minutes == 0) {
    minutes = 1;
  }
  if (minutes < 60) {
    return formatRoundedUnit(minutes, tr(STR_ETA_UNIT_MINUTE), buf, bufSize);
  }

  const uint64_t hours = (minutes + 30) / 60;
  if (hours < 24) {
    return formatRoundedUnit(hours, tr(STR_ETA_UNIT_HOUR), buf, bufSize);
  }

  const uint64_t days = (hours + 12) / 24;
  if (days < 365) {
    return formatRoundedUnit(days, tr(STR_ETA_UNIT_DAY), buf, bufSize);
  }

  const uint64_t years = (days + 182) / 365;
  return formatRoundedUnit(years, tr(STR_ETA_UNIT_YEAR), buf, bufSize);
}

bool formatRemainingFromRate(const uint32_t remainingPages, const double pagesPerMs, char* buf,
                             const size_t bufSize) {
  if (remainingPages == 0 || pagesPerMs <= 0.0) {
    return false;
  }
  const double ms = static_cast<double>(remainingPages) / pagesPerMs;
  if (ms <= 0.0 || ms >= static_cast<double>(UINT64_MAX)) {
    return false;
  }
  return formatCompactDuration(static_cast<uint64_t>(ms), buf, bufSize);
}

}  // namespace ReaderUtils
