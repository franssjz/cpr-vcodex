#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/ShortcutRegistry.h"

class ShortcutOrderActivity final : public Activity {
 public:
  ShortcutOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ShortcutOrderGroup group)
      : Activity("ShortcutOrder", renderer, mappedInput), group(group) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ShortcutOrderGroup group;
  ButtonNavigator buttonNavigator;
  std::vector<ShortcutOrderEntry> entries;
  int selectedIndex = 0;
  bool moveMode = false;

  void reloadEntries();
  void moveSelectedEntry(int delta);
  const char* getTitle() const;
};
