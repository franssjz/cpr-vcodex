#include <gtest/gtest.h>

#include <cstddef>
#include <new>

#include "lib/GfxRenderer/BitmapHelpers.h"

namespace {
int rowAllocationToFail = -1;
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (rowAllocationToFail >= 0 && rowAllocationToFail-- == 0) return nullptr;
  return ::operator new[](size);
}

TEST(DitherAllocation, ReportsEveryPartialRowAllocationFailure) {
  for (int row = 0; row < 3; ++row) {
    rowAllocationToFail = row;
    AtkinsonDitherer atkinson(8);
    rowAllocationToFail = -1;
    EXPECT_FALSE(atkinson.isValid());
    rowAllocationToFail = row;
    Atkinson1BitDitherer oneBit(8);
    rowAllocationToFail = -1;
    EXPECT_FALSE(oneBit.isValid());
  }
  for (int row = 0; row < 2; ++row) {
    rowAllocationToFail = row;
    FloydSteinbergDitherer floyd(8);
    rowAllocationToFail = -1;
    EXPECT_FALSE(floyd.isValid());
  }
}

TEST(DitherAllocation, SuccessfulBuffersRemainUsableAfterFailures) {
  AtkinsonDitherer atkinson(8);
  Atkinson1BitDitherer oneBit(8);
  FloydSteinbergDitherer floyd(8);
  ASSERT_TRUE(atkinson.isValid());
  ASSERT_TRUE(oneBit.isValid());
  ASSERT_TRUE(floyd.isValid());
  for (int row = 0; row < 16; ++row) {
    for (int x = 0; x < 8; ++x) {
      EXPECT_EQ(atkinson.processPixel(0, x), 0);
      EXPECT_EQ(floyd.processPixel(255, x), 3);
    }
    atkinson.nextRow();
    oneBit.nextRow();
    floyd.nextRow();
  }
  atkinson.reset();
  oneBit.reset();
  floyd.reset();
}
