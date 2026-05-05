#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ReadingDateSelectionActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  uint32_t initialDayOrdinal = 0;
  int selectedField = 0;
  int year = 2026;
  unsigned month = 6;
  unsigned day = 15;

  void adjustSelectedField(int delta);
  uint32_t getSelectedDayOrdinal() const;
  std::string getSelectedDateLabel() const;
  void finishWithDate();

 public:
  explicit ReadingDateSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        uint32_t initialDayOrdinal)
      : Activity("ReadingDateSelection", renderer, mappedInput), initialDayOrdinal(initialDayOrdinal) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
