#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class DictionaryActivity final : public Activity {
 public:
  explicit DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dictionary", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void selectCurrent();
};
