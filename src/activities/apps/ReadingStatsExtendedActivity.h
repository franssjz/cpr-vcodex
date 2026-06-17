#pragma once

#include "util/ButtonNavigator.h"

#include "../Activity.h"

class ReadingStatsExtendedActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int scrollOffset = 0;

 public:
  explicit ReadingStatsExtendedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsExtended", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
