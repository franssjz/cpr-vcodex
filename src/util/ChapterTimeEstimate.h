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

// Fill buf with a chapter ETA when the status-bar setting requests time and a rate exists.
// Returns true and sets *outEstimate to buf on success; otherwise false and *outEstimate unchanged.
bool tryFillStatusBarChapterEta(uint32_t remainingWords, double wordsPerMs, char* buf, size_t bufSize,
                                const char** outEstimate);

// Returns associated dwell ms to credit with page words, or 0 to skip credit.
// sameAsLastCredit requires a longer linger before re-crediting a re-read page.
uint32_t dwellCreditMs(unsigned long dwellMs, bool sameAsLastCredit);

// Shared page-dwell tracker for EPUB (spine+page) and TXT (page, id1 unused).
struct PageDwell {
  unsigned long enteredMs = 0;
  int id0 = -1;
  int id1 = -1;
  int lastCredited0 = -1;
  int lastCredited1 = -1;

  void clear();
  void restart(int a, int b, unsigned long nowMs);
  void noteEnteredIfChanged(int a, int b, unsigned long nowMs);
  uint32_t creditMs(int a, int b, unsigned long nowMs) const;
  void markCredited(int a, int b);
};

}  // namespace ChapterTimeEstimate
