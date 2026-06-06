#pragma once

#include <string>

#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingDashboardSleepScreen() const;
  void renderCoverStatsSleepScreen(bool footerOnly = false) const;
  void renderCustomStatsSleepScreen(bool footerOnly = false) const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath = "") const;
  bool renderPngSleepScreen(const std::string& sourcePath) const;
  void renderBlankSleepScreen() const;
  bool resolveLastBookCoverPath(std::string& coverBmpPath) const;
};
