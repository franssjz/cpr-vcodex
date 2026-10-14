#pragma once

#include <cstddef>
#include <vector>

#include "FlashcardsStore.h"

// Only the active study session is retained across deep sleep. Card content and
// ratings stay in the existing deck/progress files, so the snapshot stays small.
struct FlashcardReviewSession {
  std::vector<int> queue;
  size_t queueIndex = 0;
  int initialSessionSize = 0;
  bool showBack = false;
  int reviewed = 0;
  int correct = 0;
  int failed = 0;
  int skipped = 0;
  int newSeen = 0;
  int scrollLineOffset = 0;

  bool save(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress, uint8_t studyMode,
            uint8_t sessionSize) const;
  bool load(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress, uint8_t studyMode,
            uint8_t sessionSize);
};
