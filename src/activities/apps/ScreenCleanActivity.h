#pragma once

#include <cstdint>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ScreenCleanActivity final : public Activity {
  enum class Mode : uint8_t { Quick, Deep };
  enum class Pattern : uint8_t { White, Black, LightGray, DarkGray, Checker, InverseChecker };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool cleaning = false;
  bool completed = false;
  bool darkModeSaved = false;
  bool savedDarkMode = false;
  Mode mode = Mode::Quick;
  uint8_t stageIndex = 0;
  unsigned long lastStageRenderedAt = 0;

  void startCleaning(Mode cleanMode);
  void finishCleaning(bool markCompleted);
  void restoreDarkMode();
  int stageCount() const;
  Pattern patternForStage(uint8_t index) const;
  void drawPattern(Pattern pattern) const;

 public:
  explicit ScreenCleanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScreenClean", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return cleaning; }
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
