#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "FlashcardsStore.h"
#include "util/ButtonNavigator.h"

class FlashcardRecentsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<FlashcardDeckRecord> decks;
  int selectedIndex = 0;
  std::string transientMessage;
  unsigned long transientUntilMs = 0;

  void reloadDecks();
  bool openSelectedDeck();

 public:
  explicit FlashcardRecentsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardRecents", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
