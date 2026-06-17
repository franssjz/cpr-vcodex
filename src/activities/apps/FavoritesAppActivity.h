#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FavoritesAppActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int favoriteCount = 0;

  void refreshEntries();
  void openSelectedEntry();

 public:
  explicit FavoritesAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FavoritesApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
