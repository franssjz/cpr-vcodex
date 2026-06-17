#pragma once

#include <vector>

#include "../Activity.h"
#include "FlashcardsStore.h"
#include "util/ButtonNavigator.h"

class FlashcardStatsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<FlashcardDeckRecord> decks;
  int selectedIndex = 0;

  void reloadDecks();

 public:
  explicit FlashcardStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
