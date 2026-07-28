#pragma once

#include <cstdint>

// Page-dwell tracker for EPUB (spine+page) and TXT (page, id1 unused).
// clear() resets the active dwell window only; lastCredited* is kept so re-reads
// of the same page still require REREAD_MIN_MS before another credit.
struct PageDwell {
  static constexpr unsigned long MIN_DWELL_MS = 1500UL;
  static constexpr unsigned long REREAD_MIN_MS = 8000UL;
  static constexpr unsigned long MAX_DWELL_MS = 30UL * 60UL * 1000UL;

  unsigned long enteredMs = 0;
  int id0 = -1;
  int id1 = -1;
  int lastCredited0 = -1;
  int lastCredited1 = -1;

  void clear() {
    enteredMs = 0;
    id0 = -1;
    id1 = -1;
  }

  // a < 0 clears. forceRestart always resets the dwell clock; otherwise no-op
  // when already tracking (a, b).
  void noteEntered(const int a, const int b, const unsigned long nowMs, const bool forceRestart = false) {
    if (a < 0) {
      clear();
      return;
    }
    if (!forceRestart && a == id0 && b == id1 && enteredMs != 0) {
      return;
    }
    id0 = a;
    id1 = b;
    enteredMs = nowMs;
  }

  // If dwell qualifies and words > 0, marks credited and returns associated ms.
  uint32_t takeCredit(const int a, const int b, const uint32_t words, const unsigned long nowMs) {
    if (words == 0 || a != id0 || b != id1 || enteredMs == 0 || nowMs < enteredMs) {
      return 0;
    }
    const unsigned long dwellMs = nowMs - enteredMs;
    if (dwellMs < MIN_DWELL_MS) {
      return 0;
    }
    const bool sameAsLastCredit = a == lastCredited0 && b == lastCredited1;
    if (sameAsLastCredit && dwellMs < REREAD_MIN_MS) {
      return 0;
    }
    lastCredited0 = a;
    lastCredited1 = b;
    const unsigned long capped = dwellMs > MAX_DWELL_MS ? MAX_DWELL_MS : dwellMs;
    return static_cast<uint32_t>(capped);
  }
};
