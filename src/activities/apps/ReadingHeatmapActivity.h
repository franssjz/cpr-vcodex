#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ReadingHeatmapActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int viewedYear = 0;
  unsigned viewedMonth = 0;
  uint32_t selectedDayOrdinal = 0;
  bool waitForConfirmRelease = false;

  void goToAdjacentMonth(int delta);
  void resetSelectedDay();
  void moveSelection(int delta);

 public:
  explicit ReadingHeatmapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingHeatmap", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
