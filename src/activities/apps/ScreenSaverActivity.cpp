#include "ScreenSaverActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <PNGdec.h>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PngSleepRenderer.h"
#include "util/SleepImageUtils.h"

void ScreenSaverActivity::loadImages() {
  images_.clear();
  const char* dir = SETTINGS.screenSaverDirectory;
  std::string dirPath;
  if (dir[0] != '\0') {
    dirPath = dir;
  } else {
    dirPath = SleepImageUtils::resolveConfiguredSleepDirectory();
  }
  if (dirPath.empty()) return;
  images_ = SleepImageUtils::listImageFiles(dirPath);
}

unsigned long ScreenSaverActivity::getIntervalMs() const {
  switch (static_cast<CrossPointSettings::SCREENSAVER_INTERVAL>(SETTINGS.screenSaverInterval)) {
    case CrossPointSettings::SCREENSAVER_1_MIN:   return 60000UL;
    case CrossPointSettings::SCREENSAVER_5_MIN:   return 300000UL;
    case CrossPointSettings::SCREENSAVER_15_MIN:  return 900000UL;
    case CrossPointSettings::SCREENSAVER_30_MIN:  return 1800000UL;
    case CrossPointSettings::SCREENSAVER_1_HOUR:  return 3600000UL;
    case CrossPointSettings::SCREENSAVER_2_HOURS: return 7200000UL;
    case CrossPointSettings::SCREENSAVER_4_HOURS: return 14400000UL;
    case CrossPointSettings::SCREENSAVER_8_HOURS: return 28800000UL;
    default: return 1800000UL;
  }
}

int ScreenSaverActivity::getMinBatteryPercent() const {
  // 0=10%, 1=20%, ..., 8=90%
  return (static_cast<int>(SETTINGS.screenSaverMinBattery) + 1) * 10;
}

bool ScreenSaverActivity::isWakeButtonPressed() const {
  const uint8_t wakeBtn = SETTINGS.screenSaverWakeButton;
  if (wakeBtn == CrossPointSettings::SCREENSAVER_WAKE_ANY) {
    static constexpr MappedInputManager::Button allButtons[] = {
        MappedInputManager::Button::Back,    MappedInputManager::Button::Confirm,
        MappedInputManager::Button::Left,    MappedInputManager::Button::Right,
        MappedInputManager::Button::Up,      MappedInputManager::Button::Down,
        MappedInputManager::Button::Power,   MappedInputManager::Button::PageBack,
        MappedInputManager::Button::PageForward,
    };
    for (auto btn : allButtons) {
      if (mappedInput.wasPressed(btn)) return true;
    }
    return false;
  }
  static constexpr MappedInputManager::Button wakeMap[] = {
      MappedInputManager::Button::Back,
      MappedInputManager::Button::Confirm,
      MappedInputManager::Button::Left,
      MappedInputManager::Button::Right,
      MappedInputManager::Button::Up,
      MappedInputManager::Button::Down,
      MappedInputManager::Button::Power,
      MappedInputManager::Button::PageBack,
      MappedInputManager::Button::PageForward,
  };
  int idx = static_cast<int>(wakeBtn) - 1;
  if (idx >= 0 && idx < static_cast<int>(sizeof(wakeMap) / sizeof(wakeMap[0]))) {
    return mappedInput.wasPressed(wakeMap[idx]);
  }
  return false;
}

void ScreenSaverActivity::onEnter() {
  Activity::onEnter();
  loadImages();

  // Battery check: refuse to start if below minimum
  int batPct = static_cast<int>(powerManager.getBatteryPercentage());
  int minPct = getMinBatteryPercent();
  if (minPct > 0 && batPct < minPct) {
    // Show error, then go home
    {
      RenderLock lock(*this);
      renderer.clearScreen();
      GUI.drawPopup(renderer, tr(STR_BATTERY_TOO_LOW));
      delay(2000);
    }
    onGoHome();
    return;
  }

  intervalMs_ = getIntervalMs();
  lastChangeMs_ = millis();
  lastBatteryCheckMs_ = millis();
  firstRender_ = true;

  // Randomize first image in shuffle mode
  if (!images_.empty() && SETTINGS.screenSaverOrder == CrossPointSettings::SCREENSAVER_SHUFFLE) {
    currentIndex_ = random(static_cast<int>(images_.size()));
  }

  powerManager.setPowerSaving(true);
  requestUpdate();
}

void ScreenSaverActivity::onExit() {
  // Clear screen before going home - use FULL_REFRESH to avoid ghosting
  renderer.clearScreen();
  renderer.requestNextFullRefresh();
  renderer.displayBuffer();
  Activity::onExit();
  powerManager.setPowerSaving(false);
  images_.clear();
}

void ScreenSaverActivity::loop() {
  if (isWakeButtonPressed()) {
    onGoHome();
    return;
  }
  if (images_.empty()) {
    delay(500);
    if (isWakeButtonPressed()) { onGoHome(); return; }
    return;
  }

  // Periodic battery check (every 30s)
  unsigned long now = millis();
  if (now - lastBatteryCheckMs_ >= 30000UL) {
    lastBatteryCheckMs_ = now;
    int batPct = static_cast<int>(powerManager.getBatteryPercentage());
    int minPct = getMinBatteryPercent();
    if (minPct > 0 && batPct < minPct) {
      // Battery dropped below threshold -> go to deep sleep
      powerManager.setPowerSaving(false);
      powerManager.startDeepSleep(gpio);
      return;
    }
  }

  if (now - lastChangeMs_ >= intervalMs_) {
    lastChangeMs_ = now;
    if (SETTINGS.screenSaverOrder == CrossPointSettings::SCREENSAVER_SEQUENTIAL) {
      currentIndex_ = (currentIndex_ + 1) % static_cast<int>(images_.size());
    } else {
      int next = random(static_cast<int>(images_.size()));
      if (images_.size() > 1) {
        while (next == currentIndex_) { next = random(static_cast<int>(images_.size())); }
      }
      currentIndex_ = next;
    }
    requestUpdate();
    return;
  }
  delay(100);
}

void ScreenSaverActivity::render(RenderLock&&) {
  if (images_.empty() || currentIndex_ >= static_cast<int>(images_.size())) {
    renderer.clearScreen();
    renderer.displayBuffer();
    return;
  }

  const std::string& imagePath = images_[currentIndex_];
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // Use same grayscale rendering path as SleepActivity
  FsFile file;
  bool isPng = FsHelpers::hasPngExtension(imagePath);
  bool isBmp = FsHelpers::hasBmpExtension(imagePath);

  if (!isPng && !isBmp) {
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  if (isPng) {
    // PNG rendering (transparent)
    if (PngSleepRenderer::drawTransparentPng(imagePath, renderer, 0, 0, pageWidth, pageHeight)) {
      drawTextOverlay();
      renderer.clearNextRefreshOverride();
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
      return;
    }
    // Fall through to black screen on PNG failure
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  // BMP rendering with grayscale support (same as SleepActivity)
  if (!Storage.openFileForRead("SS", imagePath, file)) {
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  float cropX = 0, cropY = 0;
  int x = 0, y = 0;
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
    if (ratio > screenRatio) {
      cropX = 1.0f - (screenRatio / ratio);
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      cropY = 1.0f - (ratio / screenRatio);
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  bool hasGreyscale = bitmap.hasGreyscale();

  // BW pass
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  // Text overlay on BW
  drawTextOverlay();

  renderer.clearNextRefreshOverride();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  if (hasGreyscale) {
    // LSB pass
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    // Redraw text on LSB buffer
    drawTextOverlay();
    renderer.copyGrayscaleLsbBuffers();

    // MSB pass
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    // Redraw text on MSB buffer
    drawTextOverlay();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  file.close();
}

void ScreenSaverActivity::drawTextOverlay() {
  const char* text = SETTINGS.screenSaverText;
  if (text == nullptr || text[0] == '\0') return;

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int margin = 16;

  int fontId = UI_10_FONT_ID;
  EpdFontFamily::Style textStyle = EpdFontFamily::REGULAR;
  switch (SETTINGS.screenSaverFontSize) {
    case CrossPointSettings::SCREENSAVER_FONT_SMALL:  fontId = UI_10_FONT_ID; textStyle = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_MEDIUM: fontId = UI_12_FONT_ID; textStyle = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_LARGE:  fontId = UI_12_FONT_ID; textStyle = EpdFontFamily::BOLD; break;
  }

  const int lineHeight = renderer.getLineHeight(fontId);
  auto lines = renderer.wrappedText(fontId, text, pageWidth - 2 * margin, 4, textStyle);
  if (lines.empty()) return;

  const int textHeight = static_cast<int>(lines.size()) * lineHeight;

  bool drawPanel = SETTINGS.screenSaverShowPanel != 0;
  int pos = SETTINGS.screenSaverTextPosition;
  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_RANDOM) {
    pos = random(CrossPointSettings::SCREENSAVER_TEXT_POSITION_COUNT - 1);
  }

  int baseX = margin, baseY = margin;
  switch (pos) {
    case CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_LEFT:     baseX = margin; baseY = margin; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_RIGHT:    baseX = pageWidth - margin; baseY = margin; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_LEFT:  baseX = margin; baseY = pageHeight - margin - textHeight; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT: baseX = pageWidth - margin; baseY = pageHeight - margin - textHeight; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_CENTER:       baseX = pageWidth / 2; baseY = (pageHeight - textHeight) / 2; break;
    default: break;
  }

  int panelW = 0;
  for (auto& ln : lines) {
    int w = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    if (w > panelW) panelW = w;
  }

  int panelPadding = drawPanel ? 16 : 4;
  int panelX, panelY = baseY;

  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_RIGHT || pos == CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT) {
    panelX = pageWidth - margin - panelW - 2 * panelPadding;
  } else if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_CENTER) {
    panelX = (pageWidth - panelW) / 2 - panelPadding;
  } else {
    panelX = margin;
  }

  if (drawPanel) {
    renderer.fillRectDither(panelX, panelY, panelW + 2 * panelPadding, textHeight + 2 * panelPadding,
                            SETTINGS.screenSaverPanelColor == 0 ? Color::Black : Color::White);
  }

  int style = SETTINGS.screenSaverTextStyle;
  bool textBlack = (style == CrossPointSettings::SCREENSAVER_TEXT_BLACK || style == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);
  bool outlined = (style == CrossPointSettings::SCREENSAVER_TEXT_WHITE_OUTLINED_BLACK || style == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);

  int drawY = baseY + panelPadding;
  for (auto& ln : lines) {
    int tw = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    int dx = panelX + panelPadding + (panelW - tw) / 2;

    if (outlined) {
      renderer.drawText(fontId, dx - 2, drawY, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 2, drawY, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx, drawY - 2, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx, drawY + 2, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx - 1, drawY - 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 1, drawY - 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx - 1, drawY + 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 1, drawY + 1, ln.c_str(), !textBlack, textStyle);
    }
    renderer.drawText(fontId, dx, drawY, ln.c_str(), textBlack, textStyle);
    drawY += lineHeight;
  }
}