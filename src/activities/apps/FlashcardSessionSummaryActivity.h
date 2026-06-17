#pragma once

#include "../Activity.h"
#include "activities/ActivityResult.h"

class FlashcardSessionSummaryActivity final : public Activity {
  FlashcardSessionResult summary;

 public:
  FlashcardSessionSummaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, FlashcardSessionResult summary)
      : Activity("FlashcardSessionSummary", renderer, mappedInput), summary(std::move(summary)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
