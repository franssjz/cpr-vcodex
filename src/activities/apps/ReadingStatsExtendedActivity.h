#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ReadingStatsExtendedActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int scrollOffset = 0;

 public:
  explicit ReadingStatsExtendedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsExtended", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
