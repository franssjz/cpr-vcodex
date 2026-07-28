#include "util/ChapterTimeEstimate.h"

#include <CrossPointSettings.h>
#include <I18n.h>

#include <cstdio>

namespace ChapterTimeEstimate {
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

  const uint64_t hours = (minutes + 30) / 60;  // minutes >= 60 ⇒ hours >= 1
  if (hours < 24) {
    return formatRoundedUnit(hours, tr(STR_ETA_UNIT_HOUR), buf, bufSize);
  }

  const uint64_t days = (hours + 12) / 24;  // hours >= 24 ⇒ days >= 1
  if (days < 365) {
    return formatRoundedUnit(days, tr(STR_ETA_UNIT_DAY), buf, bufSize);
  }

  const uint64_t years = (days + 182) / 365;  // days >= 365 ⇒ years >= 1
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

bool tryFillStatusBarChapterEta(const uint32_t remainingWords, const double wordsPerMs, char* buf,
                                const size_t bufSize, const char** outEstimate) {
  if (!outEstimate || !statusBarWantsChapterTime()) {
    return false;
  }
  if (!formatRemainingFromRate(remainingWords, wordsPerMs, buf, bufSize)) {
    return false;
  }
  *outEstimate = buf;
  return true;
}

void PageDwell::clear() {
  enteredMs = 0;
  id0 = -1;
  id1 = -1;
}

void PageDwell::restart(const int a, const int b, const unsigned long nowMs) {
  id0 = a;
  id1 = b;
  enteredMs = nowMs;
}

void PageDwell::noteEnteredIfChanged(const int a, const int b, const unsigned long nowMs) {
  if (a == id0 && b == id1 && enteredMs != 0) {
    return;
  }
  restart(a, b, nowMs);
}

uint32_t PageDwell::creditMs(const int a, const int b, const unsigned long nowMs) const {
  if (a != id0 || b != id1 || enteredMs == 0) {
    return 0;
  }
  return dwellCreditMs(nowMs - enteredMs, a == lastCredited0 && b == lastCredited1);
}

void PageDwell::markCredited(const int a, const int b) {
  lastCredited0 = a;
  lastCredited1 = b;
}

uint32_t takeDwellCreditMs(PageDwell& dwell, const int id0, const int id1, const uint32_t words,
                           const unsigned long nowMs) {
  if (words == 0) {
    return 0;
  }
  const uint32_t associatedMs = dwell.creditMs(id0, id1, nowMs);
  if (associatedMs == 0) {
    return 0;
  }
  dwell.markCredited(id0, id1);
  return associatedMs;
}

}  // namespace ChapterTimeEstimate
