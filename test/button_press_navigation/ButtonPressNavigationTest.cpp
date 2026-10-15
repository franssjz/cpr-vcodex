#include <gtest/gtest.h>

#include "util/ButtonNavigator.h"

unsigned long testNowMs = 1000;

namespace {
constexpr uint8_t PREVIOUS = 1;
constexpr uint8_t NEXT = 2;

class ButtonPressNavigationTest : public ::testing::Test {
 protected:
  MappedInputManager input;
  ButtonNavigator navigator;
  int selected = 0;
  int pages = 0;

  void SetUp() override {
    testNowMs = 1000;
    ButtonNavigator::setMappedInputManager(input);
  }

  // The shared list call order, with the production navigator.
  void navigate() {
    navigator.onNextPress([this] { selected = ButtonNavigator::nextIndex(selected, 10); });
    navigator.onPreviousPress([this] { selected = ButtonNavigator::previousIndex(selected, 10); });
    navigator.onNextContinuous([this] { ++pages; });
    navigator.onPreviousContinuous([this] { --pages; });
  }

  // A render/preemption can advance the live hold clock before the same
  // sampled input frame reaches its long-press query.
  void navigateOnce(uint32_t elapsedDuringPress = 0) {
    navigator.onNextPress([&] {
      selected = ButtonNavigator::nextIndex(selected, 10);
      testNowMs += elapsedDuringPress;
      input.frame.heldMs += elapsedDuringPress;
    });
    navigator.onPreviousPress([&] {
      selected = ButtonNavigator::previousIndex(selected, 10);
      testNowMs += elapsedDuringPress;
      input.frame.heldMs += elapsedDuringPress;
    });
    navigator.onNextLongPressOnce([this] { ++pages; });
    navigator.onPreviousLongPressOnce([this] { --pages; });
  }
};

TEST_F(ButtonPressNavigationTest, PressStepsImmediatelyAndReleaseDoesNotStep) {
  input.frame = {0, NEXT, 0, NEXT};
  navigate();
  EXPECT_EQ(selected, 1);
  input.frame = {90, 0, NEXT, 0};
  navigate();
  EXPECT_EQ(selected, 1);
  EXPECT_EQ(pages, 0);
}

TEST_F(ButtonPressNavigationTest, HoldPagesAfterThresholdAndStopsOnRelease) {
  input.frame = {0, NEXT, 0, NEXT};
  navigate();
  testNowMs += 501;
  input.frame = {501, 0, 0, NEXT};
  navigate();
  EXPECT_EQ(selected, 1);
  EXPECT_EQ(pages, 1);
  testNowMs += 100;
  input.frame.heldMs += 100;
  navigate();
  EXPECT_EQ(pages, 1);
  testNowMs += 1000;
  input.frame = {1601, 0, NEXT, 0};
  navigate();
  EXPECT_EQ(pages, 1);
}

TEST_F(ButtonPressNavigationTest, PressNeverAlsoRepeatsEvenWithOldHeldDuration) {
  input.frame = {2000, NEXT, 0, NEXT};
  navigate();
  EXPECT_EQ(selected, 1);
  EXPECT_EQ(pages, 0);
}

TEST_F(ButtonPressNavigationTest, FastDirectionChangeStartsANewRepeatInterval) {
  input.frame = {501, 0, 0, NEXT};
  navigate();
  EXPECT_EQ(pages, 1);
  input.frame = {0, PREVIOUS, NEXT, PREVIOUS};
  navigate();
  EXPECT_EQ(selected, 9);
  EXPECT_EQ(pages, 1);
}

TEST_F(ButtonPressNavigationTest, SlowPressFrameCannotAlsoConsumeLongPress) {
  input.sample({0, NEXT, 0, NEXT});
  navigateOnce(700);
  EXPECT_EQ(selected, 1);
  EXPECT_EQ(pages, 0);
  EXPECT_EQ(input.longPressQueries[1], 0u);
  // A short physical tap can already be up when the next sample is taken.
  input.sample({80, 0, NEXT, 0});
  navigateOnce();
  EXPECT_EQ(pages, 0);
  EXPECT_EQ(input.longPressQueries[1], 0u);
}

TEST_F(ButtonPressNavigationTest, LaterHeldFrameFiresOnceAfterSlowPressFrame) {
  input.sample({0, NEXT, 0, NEXT});
  navigateOnce(700);
  EXPECT_EQ(pages, 0);
  input.sample({700, 0, 0, NEXT});
  navigateOnce();
  EXPECT_EQ(pages, 1);
  EXPECT_EQ(input.longPressQueries[1], 1u);
  input.sample({1700, 0, 0, NEXT});
  navigateOnce();
  EXPECT_EQ(pages, 1);
}

TEST_F(ButtonPressNavigationTest, SlowReleaseFrameNeverQueriesLongPressLatch) {
  input.sample({80, 0, NEXT, 0});
  navigator.onNextRelease([this] {
    ++selected;
    testNowMs += 700;
    input.frame.heldMs += 700;
  });
  navigator.onNextLongPressOnce([this] { ++pages; });
  EXPECT_EQ(selected, 1);
  EXPECT_EQ(pages, 0);
  EXPECT_EQ(input.longPressQueries[1], 0u);
}

TEST_F(ButtonPressNavigationTest, DirectionChangeRearmsEachOneShotHold) {
  input.sample({0, NEXT, 0, NEXT});
  navigateOnce();
  input.sample({700, 0, 0, NEXT});
  navigateOnce();
  EXPECT_EQ(pages, 1);
  const auto previousQueries = input.longPressQueries[0];
  input.sample({0, PREVIOUS, NEXT, PREVIOUS});
  navigateOnce(700);
  EXPECT_EQ(selected, 0);
  EXPECT_EQ(pages, 1);
  EXPECT_EQ(input.longPressQueries[0], previousQueries);
  input.sample({700, 0, 0, PREVIOUS});
  navigateOnce();
  EXPECT_EQ(pages, 0);
  input.sample({1700, 0, 0, PREVIOUS});
  navigateOnce();
  EXPECT_EQ(pages, 0);
  input.sample({0, NEXT, PREVIOUS, NEXT});
  navigateOnce(700);
  EXPECT_EQ(pages, 0);
  input.sample({700, 0, 0, NEXT});
  navigateOnce();
  EXPECT_EQ(pages, 1);
}

}  // namespace
