#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class SleepPreviewActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string directoryPath;
  std::vector<std::string> imagePaths;
  int selectedIndex = 0;

  void loadImages();
  void renderPreview(bool showLoadingPopup);
  void selectDirectory();
  void showLoadError(const char* message);

 public:
  SleepPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string directoryPath)
      : Activity("SleepPreview", renderer, mappedInput), directoryPath(std::move(directoryPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
