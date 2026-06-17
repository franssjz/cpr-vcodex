#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FlashcardSettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  int getSettingCount() const { return 2; }
  void toggleSelectedSetting();

 public:
  explicit FlashcardSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
