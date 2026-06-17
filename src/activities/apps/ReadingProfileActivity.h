#pragma once

#include <I18n.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../Activity.h"

struct ReadingProfileAxisSummary {
  StrId labelId = StrId::STR_NONE_OPT;
  int score = 0;
  std::string primaryValue;
  StrId primaryLabelId = StrId::STR_NONE_OPT;
  std::string secondaryValue;
  StrId secondaryLabelId = StrId::STR_NONE_OPT;
  std::string tertiaryValue;
  StrId tertiaryLabelId = StrId::STR_NONE_OPT;
};

struct ReadingProfileSummary {
  int totalScore = 0;
  int daysRead = 0;
  int goalDays = 0;
  int longestReadStreak = 0;
  int bestDaySharePercent = 0;
  int sessions = 0;
  int sessionsPerReadDayTenths = 0;
  int sessionsUnder10mPercent = 0;
  int sessions10to29mPercent = 0;
  int sessions30mPlusPercent = 0;
  ReadingProfileAxisSummary habit;
  ReadingProfileAxisSummary stability;
  ReadingProfileAxisSummary engagement;
  ReadingProfileAxisSummary depth;
};

struct ReadingProfileMetricCardCache {
  std::vector<std::string> primaryLabelLines;
  std::vector<std::string> secondaryLabelLines;
  std::vector<std::string> tertiaryLabelLines;
};

class ReadingProfileActivity final : public Activity {
  int scrollOffset = 0;
  int maxScrollOffset = 0;
  uint32_t lastScrollActionMs = 0;
  int scrollDirection = 0;
  bool profileCacheValid = false;
  ReadingProfileSummary profileSummary;
  std::array<std::vector<std::string>, 4> cachedAxisLabelLines;
  std::array<std::vector<std::string>, 4> cachedSectionDescriptionLines;
  std::array<ReadingProfileMetricCardCache, 4> cachedMetricCardLines;
  std::array<int, 4> cachedSectionTops = {};
  std::array<int, 4> cachedSectionCardsTops = {};
  std::array<int, 4> cachedSectionBottoms = {};
  std::string cachedTitle;
  int cachedRadarTop = 0;
  int cachedScoreTop = 0;
  int cachedContentBottom = 0;

  void rebuildProfileCache();

 public:
  explicit ReadingProfileActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingProfile", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
