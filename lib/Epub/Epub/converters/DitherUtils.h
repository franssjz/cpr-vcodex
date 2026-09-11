#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Quantize to the panel's four physical gray levels while distributing the
// fractional level over a 4x4 Bayer cell. This preserves the average luminance
// across the full 0..255 range instead of crushing values around the fixed
// quarter-tone thresholds. Stateless: safe for tiled or out-of-order decoding.
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  constexpr uint16_t maxLevel = 3;
  constexpr uint16_t inputRange = 255;
  const uint16_t scaled = static_cast<uint16_t>(gray) * maxLevel;
  uint8_t level = static_cast<uint8_t>(scaled / inputRange);
  const uint16_t remainder = scaled % inputRange;

  // Cell-centred thresholds 8, 24, ... 248 give an even 16-step coverage.
  const uint16_t threshold = static_cast<uint16_t>(bayer4x4[y & 3][x & 3]) * 16 + 8;
  if (level < maxLevel && remainder > threshold) {
    ++level;
  }
  return level;
}
