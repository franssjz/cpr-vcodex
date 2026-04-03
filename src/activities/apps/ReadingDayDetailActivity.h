#pragma once

#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include "util/ReadingStatsAnalytics.h"

class ReadingDayDetailActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  uint32_t dayOrdinal = 0;
  int selectedIndex = 0;
  std::vector<ReadingStatsAnalytics::DayBookEntry> entries;
  bool waitForConfirmRelease = false;

  void refreshEntries();
  void openSelectedBook();

 public:
  explicit ReadingDayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t dayOrdinal)
      : Activity("ReadingDayDetail", renderer, mappedInput), dayOrdinal(dayOrdinal) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
