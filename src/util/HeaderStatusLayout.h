#pragma once

#include <algorithm>

namespace headerstatus {
struct Interval {
  int left;
  int right;
  int width() const { return std::max(0, right - left); }
};

// Use the same fixed battery/clock slots as FreeInkUI's header. Prefer the
// right-hand space for the fork's date unless it only fits on the left.
inline Interval freeInterval(int bandLeft, int bandRight, int inset, int batteryWidth, bool batteryLeft, int clockWidth,
                             bool clockCentered, int requestedWidth) {
  constexpr int gap = 8;
  Interval available{bandLeft + inset, bandRight - inset};
  if (batteryWidth > 0) {
    if (batteryLeft)
      available.left += batteryWidth + gap;
    else
      available.right -= batteryWidth + gap;
  }
  if (clockWidth <= 0) return available;
  if (!clockCentered) {
    if (batteryLeft)
      available.right -= clockWidth + gap;
    else
      available.left += clockWidth + gap;
    return available;
  }
  const int clockLeft = bandLeft + (bandRight - bandLeft - clockWidth) / 2;
  const Interval left{available.left, std::min(available.right, clockLeft - gap)};
  const Interval right{std::max(available.left, clockLeft + clockWidth + gap), available.right};
  return right.width() >= requestedWidth || right.width() >= left.width() ? right : left;
}
}  // namespace headerstatus
