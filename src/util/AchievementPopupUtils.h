#pragma once

#include <Arduino.h>
#include <GfxRenderer.h>

#include <string>

#include "AchievementsStore.h"
#include "components/UITheme.h"

inline bool showPendingAchievementPopups(GfxRenderer& renderer, const unsigned long delayMs = 700) {
  if (!SETTINGS.achievementPopups) {
    ACHIEVEMENTS.clearPendingUnlocks();
    return false;
  }

  bool showedAny = false;
  while (ACHIEVEMENTS.hasPendingUnlocks()) {
    const std::string message = ACHIEVEMENTS.popNextPopupMessage();
    if (message.empty()) {
      break;
    }
    GUI.drawPopup(renderer, message.c_str());
    delay(delayMs);
    showedAny = true;
  }
  return showedAny;
}
