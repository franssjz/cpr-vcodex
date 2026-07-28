#include "util/ChapterTimeEstimate.h"

#include <CrossPointSettings.h>
#include <I18n.h>
#include <ReadingStatsStore.h>

#include <cstdio>

namespace ChapterTimeEstimate {
namespace {
constexpr uint64_t MS_PER_MINUTE = 60ULL * 1000ULL;
constexpr uint64_t MS_PER_HOUR = 60ULL * MS_PER_MINUTE;
constexpr uint64_t MS_PER_DAY = 24ULL * MS_PER_HOUR;
constexpr uint64_t MS_PER_YEAR = 365ULL * MS_PER_DAY;

bool formatRoundedUnit(const uint64_t value, const char* unit, char* buf, const size_t bufSize) {
  if (!unit || unit[0] == '\0') {
    return false;
  }
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

  uint64_t hours = (minutes + 30) / 60;
  if (hours == 0) {
    hours = 1;
  }
  if (hours < 24) {
    return formatRoundedUnit(hours, tr(STR_ETA_UNIT_HOUR), buf, bufSize);
  }

  uint64_t days = (hours + 12) / 24;
  if (days == 0) {
    days = 1;
  }
  if (days < 365) {
    return formatRoundedUnit(days, tr(STR_ETA_UNIT_DAY), buf, bufSize);
  }

  uint64_t years = (days + 182) / 365;
  if (years == 0) {
    years = 1;
  }
  return formatRoundedUnit(years, tr(STR_ETA_UNIT_YEAR), buf, bufSize);
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

bool statusBarWantsChapterTime() {
  return SETTINGS.statusBarChapterProgress == CrossPointSettings::CHAPTER_PROGRESS_PAGES_TIME ||
         SETTINGS.statusBarChapterProgress == CrossPointSettings::CHAPTER_PROGRESS_TIME;
}

bool tryFillStatusBarChapterEta(const uint32_t remainingWords, char* buf, const size_t bufSize,
                                const char** outEstimate) {
  if (!outEstimate || !statusBarWantsChapterTime()) {
    return false;
  }
  if (!formatRemainingFromRate(remainingWords, READING_STATS.getEffectiveWordsPerMs(), buf, bufSize)) {
    return false;
  }
  *outEstimate = buf;
  return true;
}

uint32_t dwellCreditMs(const unsigned long dwellMs, const bool sameAsLastCredit) {
  if (dwellMs < MIN_DWELL_MS) {
    return 0;
  }
  if (sameAsLastCredit && dwellMs < REREAD_MIN_MS) {
    return 0;
  }
  const unsigned long capped = dwellMs > MAX_DWELL_MS ? MAX_DWELL_MS : dwellMs;
  return static_cast<uint32_t>(capped);
}

}  // namespace ChapterTimeEstimate
