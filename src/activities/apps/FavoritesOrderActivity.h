#pragma once

#include <vector>

#include "FavoritesStore.h"
#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FavoritesOrderActivity final : public Activity {
 public:
  FavoritesOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FavoritesOrder", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::vector<FavoriteBook> entries;
  int selectedIndex = 0;
  bool moveMode = false;

  void reloadEntries();
  void moveSelectedEntry(int delta);
  void confirmDeleteSelectedEntry();
};
