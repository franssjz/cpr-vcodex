#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ManualDateActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedField = 0;
  int year = 2026;
  unsigned month = 6;
  unsigned day = 15;

  void adjustSelectedField(int delta);
  void saveDate();
  std::string getSelectedDateLabel() const;

 public:
  explicit ManualDateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ManualDate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
