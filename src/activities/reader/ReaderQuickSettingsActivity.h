#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderQuickSettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedTab = 0;
  int selectedIndex = 0;
  bool tabFocused = true;

  void adjustSelected(int direction);
  int currentItemCount() const;

 public:
  explicit ReaderQuickSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReaderQuickSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
