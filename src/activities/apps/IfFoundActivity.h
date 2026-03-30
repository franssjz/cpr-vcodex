#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class IfFoundActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<std::string> introLines;
  std::vector<std::string> bodyLines;
  int scrollOffset = 0;

  void loadContent();
  int getVisibleBodyLineCount() const;
  int getMaxScrollOffset() const;

 public:
  explicit IfFoundActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("IfFound", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
