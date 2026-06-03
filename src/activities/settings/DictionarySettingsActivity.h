#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Activity for managing StarDict dictionaries:
 * toggle enabled/disabled and reorder.
 */
class DictionarySettingsActivity final : public Activity {
 public:
  explicit DictionarySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DictionarySettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int totalItems = 0;
};
