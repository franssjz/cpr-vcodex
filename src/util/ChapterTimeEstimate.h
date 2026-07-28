#pragma once

#include <cstddef>
#include <cstdint>

namespace ChapterTimeEstimate {

// Dwell thresholds for pairing page word credits with reading time.
constexpr unsigned long MIN_DWELL_MS = 1500UL;
constexpr unsigned long REREAD_MIN_MS = 8000UL;
constexpr unsigned long MAX_DWELL_MS = 30UL * 60UL * 1000UL;

// Compact single-unit duration for the status bar: 15m / 2h / 3d / 1y.
// Returns false when buf is too small or ms is zero (nothing to show).
bool formatCompactDuration(uint64_t totalMs, char* buf, size_t bufSize);

// Format remaining chapter time from remaining words and words/ms rate.
// Returns false when inputs are insufficient or the buffer is too small.
bool formatRemainingFromRate(uint32_t remainingWords, double wordsPerMs, char* buf, size_t bufSize);

// True when the status-bar chapter setting wants a time estimate shown.
bool statusBarWantsChapterTime();

// Compose "Pages+Time" from STR_PAGES + '+' + STR_TIME (no dedicated i18n key).
bool formatPagesPlusTime(char* buf, size_t bufSize);

// Label for statusBarChapterProgress enum index. Always use this instead of raw
// StrId lookup — index CHAPTER_PROGRESS_PAGES_TIME is composed, and settings
// tables may store a placeholder StrId for that slot.
bool formatChapterProgressLabel(uint8_t mode, char* buf, size_t bufSize);

// Shared page-dwell tracker for EPUB (spine+page) and TXT (page, id1 unused).
// clear() resets the active dwell window only; lastCredited* is kept so re-reads
// of the same page still require REREAD_MIN_MS before another credit.
struct PageDwell {
  unsigned long enteredMs = 0;
  int id0 = -1;
  int id1 = -1;
  int lastCredited0 = -1;
  int lastCredited1 = -1;

  void clear();
  // a < 0 clears. forceRestart always resets the dwell clock; otherwise no-op
  // when already tracking (a, b).
  void noteEntered(int a, int b, unsigned long nowMs, bool forceRestart = false);
  // If dwell qualifies and words > 0, marks credited and returns associated ms.
  uint32_t takeCredit(int a, int b, uint32_t words, unsigned long nowMs);
};

}  // namespace ChapterTimeEstimate
