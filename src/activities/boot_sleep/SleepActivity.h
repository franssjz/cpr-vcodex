#pragma once

#include <string>

#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;

  // Pick a fresh image from the configured sleep directory and draw it without
  // any popup or text. Used by the deep-sleep tap-to-cycle path: APP_STATE must
  // already be loaded; the renderer and display must already be initialized;
  // fonts are not required because only a BMP/PNG is drawn. No-op if no usable
  // image is found — the existing on-screen image stays visible.
  static void cycleScreensaverFromDeepSleep(GfxRenderer& renderer);

  // Snapshot the current framebuffer to SD so the cycle path can re-use it as
  // the background behind a transparent sleep PNG without needing fonts or the
  // EPUB parser. Called from SleepActivity::onEnter() before the "Going to
  // sleep" popup is drawn over the reader page.
  static void snapshotFramebufferForCycle();

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
