#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

struct ReadingBookStats;

class ReadingStatsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  void openSelectedEntry();
  void confirmRemoveSelectedBook();
  void guardBackReturn();

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
