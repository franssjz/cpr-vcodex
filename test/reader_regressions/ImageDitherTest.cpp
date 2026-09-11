#include <gtest/gtest.h>

#include <cstdint>

#include "lib/Epub/Epub/converters/DitherUtils.h"

namespace {
int bayerCellSum(const uint8_t gray) {
  int sum = 0;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const uint8_t level = applyBayerDither4Level(gray, x, y);
      EXPECT_LE(level, 3);
      sum += level;
    }
  }
  return sum;
}
}  // namespace

TEST(ImageDitherRegression, KeepsBlackAndWhiteAtThePanelEndpoints) {
  EXPECT_EQ(bayerCellSum(0), 0);
  EXPECT_EQ(bayerCellSum(255), 48);
}

TEST(ImageDitherRegression, PreservesAverageLuminanceIncludingDarkDetails) {
  for (int gray = 0; gray <= 255; ++gray) {
    const int actual = bayerCellSum(static_cast<uint8_t>(gray));
    const int expected = (gray * 48 + 127) / 255;
    EXPECT_LE(actual > expected ? actual - expected : expected - actual, 1) << "gray=" << gray;
  }

  // The old fixed-threshold implementation rendered this entire dark Bayer
  // cell black. A proportional cell must retain visible texture.
  EXPECT_GT(bayerCellSum(42), 0);
}

TEST(ImageDitherRegression, AverageOutputIsMonotonicAcrossTheInputRange) {
  int previous = -1;
  for (int gray = 0; gray <= 255; ++gray) {
    const int current = bayerCellSum(static_cast<uint8_t>(gray));
    EXPECT_GE(current, previous) << "gray=" << gray;
    previous = current;
  }
}
