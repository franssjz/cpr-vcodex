#pragma once

#include <cstddef>
#include <cstdint>

namespace ChapterTimeEstimate {

// Compact single-unit duration for the status bar: 15m / 2h / 3d / 1y.
// Returns false when buf is too small or ms is zero (nothing to show).
bool formatCompactDuration(uint64_t totalMs, char* buf, size_t bufSize);

// Format remaining chapter time from remaining words and words/ms rate.
// Returns false when inputs are insufficient or the buffer is too small.
bool formatRemainingFromRate(uint32_t remainingWords, double wordsPerMs, char* buf, size_t bufSize);

}  // namespace ChapterTimeEstimate
