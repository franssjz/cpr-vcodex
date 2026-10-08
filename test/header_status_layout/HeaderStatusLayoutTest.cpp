#include <gtest/gtest.h>

#include "HeaderStatusLayout.h"

TEST(HeaderStatusLayout, DateReservesBatteryPercentAndTerminal) {
  const auto area = headerstatus::freeInterval(0, 480, 24, 60, false, 0, false, 100);
  EXPECT_EQ(area.left, 24);
  EXPECT_EQ(area.right, 388);
}

TEST(HeaderStatusLayout, CornerClockAndLeftBatteryLeaveTheMiddleFree) {
  const auto area = headerstatus::freeInterval(10, 470, 20, 60, true, 50, false, 100);
  EXPECT_EQ(area.left, 98);
  EXPECT_EQ(area.right, 392);
}

TEST(HeaderStatusLayout, ShortDateFitsRightOfCenteredClock) {
  const auto area = headerstatus::freeInterval(0, 480, 24, 60, false, 60, true, 100);
  EXPECT_EQ(area.left, 278);
  EXPECT_EQ(area.right, 388);
}

TEST(HeaderStatusLayout, LongDateUsesLargerSpaceLeftOfClock) {
  const auto area = headerstatus::freeInterval(0, 480, 24, 60, false, 60, true, 180);
  EXPECT_EQ(area.left, 24);
  EXPECT_EQ(area.right, 202);
}

TEST(HeaderStatusLayout, NarrowDisplayHasNoNegativeTextWidth) {
  const auto area = headerstatus::freeInterval(0, 90, 24, 60, false, 60, true, 100);
  EXPECT_EQ(area.width(), 0);
}
