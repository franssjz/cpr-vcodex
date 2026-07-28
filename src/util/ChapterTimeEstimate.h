#pragma once

#include <cstddef>
#include <cstdint>

namespace ChapterTimeEstimate {

// Compact single-unit duration for the status bar: 15m / 2h / 3d / 1y.
// Returns false when buf is too small or ms is zero (nothing to show).
bool formatCompactDuration(uint64_t totalMs, char* buf, size_t bufSize);

// Estimate chapter remaining time from remaining words and an effective words/ms rate.
// Returns 0 when inputs are insufficient.
uint64_t estimateRemainingMs(uint32_t remainingWords, double wordsPerMs);

}  // namespace ChapterTimeEstimate
