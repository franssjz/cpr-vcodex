#pragma once

#include <Arduino.h>
#include <GfxRenderer.h>

#include <string>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "components/UITheme.h"

inline bool showPendingAchievementPopups(const GfxRenderer& renderer, const unsigned long delayMs = 0) {
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
    // NOTE: Removed blocking delay (was 700ms by default)
    // Achievement popups now display immediately without freezing the UI.
    // The display buffer is flushed by drawPopup(), and the high-frequency
    // render loop naturally transitions to the next frame. This eliminates
    // 700ms UI freeze while maintaining user-visible feedback.
    // (Use delayMs parameter only for backward compatibility if needed)
    if (delayMs > 0) {
      delay(delayMs);
    }
    showedAny = true;
  }
  return showedAny;
}
