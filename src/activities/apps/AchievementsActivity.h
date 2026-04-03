#pragma once

#include <vector>

#include "../Activity.h"
#include "AchievementsStore.h"
#include "util/ButtonNavigator.h"

class AchievementsActivity final : public Activity {
  enum class FilterTab : uint8_t { Pending = 0, Completed };

  ButtonNavigator buttonNavigator;
  bool waitForConfirmRelease = false;
  FilterTab selectedTab = FilterTab::Pending;
  int selectedIndex = 0;
  std::vector<AchievementView> achievements;
  std::vector<int> visibleIndexes;

  void refreshEntries();
  void rebuildVisibleIndexes();

 public:
  explicit AchievementsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Achievements", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
