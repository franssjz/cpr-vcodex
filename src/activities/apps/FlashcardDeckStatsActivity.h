#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "FlashcardsStore.h"

class FlashcardDeckStatsActivity final : public Activity {
  std::string deckPath;
  FlashcardDeck deck;
  std::vector<FlashcardCardProgress> progress;
  FlashcardDeckMetrics metrics;
  bool loaded = false;
  std::string errorMessage;

  void loadDeckData();

 public:
  FlashcardDeckStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath)
      : Activity("FlashcardDeckStats", renderer, mappedInput), deckPath(std::move(deckPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
