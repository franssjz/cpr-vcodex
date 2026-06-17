#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FavoritesBrowserActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  bool lockLongPressBack = false;
  std::string basepath = "/";
  std::vector<std::string> files;
  std::vector<uint8_t> favoriteStates;

  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FavoritesBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    std::string initialPath = "/")
      : Activity("FavoritesBrowser", renderer, mappedInput),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
