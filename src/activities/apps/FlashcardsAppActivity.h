#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FlashcardsAppActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int recentCount = 0;
  int deckCount = 0;

  void refreshCounts();
  void openSelectedEntry();

 public:
  explicit FlashcardsAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardsApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
