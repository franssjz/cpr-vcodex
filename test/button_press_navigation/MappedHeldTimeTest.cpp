#include <gtest/gtest.h>

#include "MappedHeldTimeAdapter.h"

namespace {
class MappedHeldTime : public ::testing::Test {
 protected:
  HalGPIO gpio;
  MappedInputManager input{gpio};
  void SetUp() override { heldTestNowMs = 1000; }
  void press() {
    gpio.pendingHeld = true;
    input.update();
  }
};

TEST_F(MappedHeldTime, SlowHeldFrameCannotAgeIntoLongPress) {
  press();
  heldTestNowMs += 80;
  input.update();  // A later held frame, with no press/release edge.
  EXPECT_FALSE(gpio.wasAnyPressed());
  EXPECT_FALSE(gpio.wasAnyReleased());
  heldTestNowMs += 700;  // Rendering delays the handlers for this same sample.
  EXPECT_EQ(gpio.getHeldTime(), 780u);
  EXPECT_EQ(input.getHeldTime(), 80u);
  EXPECT_FALSE(input.wasLongPressed(MappedInputManager::Button::NavNext, 500));
  EXPECT_EQ(input.suppressedReleaseButtons, 0);
}

TEST_F(MappedHeldTime, GenuineHoldAdvancesOnNextSampleAndFiresOnce) {
  press();
  heldTestNowMs += 600;
  EXPECT_EQ(input.getHeldTime(), 0u);
  input.update();
  EXPECT_EQ(input.getHeldTime(), 600u);
  EXPECT_TRUE(input.wasLongPressed(MappedInputManager::Button::NavNext, 500));
  EXPECT_FALSE(input.wasLongPressed(MappedInputManager::Button::NavNext, 500));
  heldTestNowMs += 1000;
  input.update();
  EXPECT_FALSE(input.wasLongPressed(MappedInputManager::Button::NavNext, 500));
  gpio.pendingHeld = false;
  gpio.pendingReleaseDuration = 1600;
  input.update();
  press();
  heldTestNowMs += 600;
  input.update();
  EXPECT_TRUE(input.wasLongPressed(MappedInputManager::Button::NavNext, 500));
}

TEST_F(MappedHeldTime, ReleaseKeepsLatchedPhysicalDurationDuringSlowHandler) {
  press();
  heldTestNowMs += 700;
  gpio.pendingHeld = false;
  gpio.pendingReleaseDuration = 80;
  input.update();
  EXPECT_TRUE(gpio.wasAnyReleased());
  EXPECT_EQ(input.getHeldTime(), 80u);
  heldTestNowMs += 1000;
  EXPECT_EQ(input.getHeldTime(), 80u);
  EXPECT_FALSE(input.wasLongPressed(MappedInputManager::Button::NavNext, 500));
}

TEST_F(MappedHeldTime, HomeAndTouchOverridesRetainTheirExistingSemantics) {
  input.update();
  input.homeAction = HomeButtonAction::Confirm;
  EXPECT_EQ(input.getHeldTime(), 0u);
  input.homeAction = HomeButtonAction::Ignore;
  input.touchHeldOverrideValid = true;
  input.touchHeldOverrideMs = 1300;
  input.touchHeldOverrideAt = millis();
  EXPECT_EQ(input.getHeldTime(), 1300u);
  heldTestNowMs += TOUCH_HELD_OVERRIDE_WINDOW_MS + 1;
  EXPECT_EQ(input.getHeldTime(), 0u);
  EXPECT_FALSE(input.touchHeldOverrideValid);
  input.touchHeldOverrideValid = true;
  input.touchHeldOverrideAt = millis();
  press();
  EXPECT_EQ(input.getHeldTime(), 0u);
  EXPECT_FALSE(input.touchHeldOverrideValid);
}
}  // namespace
