#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "FlashcardsStore.h"

class FlashcardReviewActivity final : public Activity {
  std::string deckPath;
  FlashcardDeck deck;
  std::vector<FlashcardCardProgress> progress;
  std::vector<int> queue;
  size_t queueIndex = 0;
  int initialSessionSize = 0;
  bool showBack = false;
  bool loaded = false;
  std::string errorMessage;
  GfxRenderer::Orientation originalOrientation = GfxRenderer::Orientation::Portrait;
  bool orientationApplied = false;

  int sessionReviewed = 0;
  int sessionCorrect = 0;
  int sessionFailed = 0;
  int sessionSkipped = 0;
  int sessionNewSeen = 0;
  std::vector<std::string> newlySeenKeys;

  void loadDeckData();
  void finishWithSummary();
  bool isCurrentCardUnseen() const;
  FlashcardCardProgress& currentProgress();
  const FlashcardCard& currentCard() const;
  void goToNextCard();
  void markCurrentSuccess();
  void markCurrentFailure();
  void skipCurrentCard();

 public:
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath)
      : Activity("FlashcardReview", renderer, mappedInput), deckPath(std::move(deckPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
