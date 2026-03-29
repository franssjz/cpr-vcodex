#pragma once

#include <vector>

#include "../Activity.h"
#include "util/ShortcutRegistry.h"
#include "util/ButtonNavigator.h"

class AppsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::vector<const ShortcutDefinition*> appShortcuts;

  void openSelectedApp();

 public:
  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Apps", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
