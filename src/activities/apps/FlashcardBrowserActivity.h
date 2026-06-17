#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FlashcardBrowserActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string basepath = "/";
  std::vector<std::string> files;
  size_t selectorIndex = 0;
  bool lockLongPressBack = false;
  std::string transientMessage;
  unsigned long transientUntilMs = 0;

  void loadFiles();
  size_t findEntry(const std::string& name) const;
  bool openDeckPath(const std::string& path);

 public:
  explicit FlashcardBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
