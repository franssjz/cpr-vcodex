#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <algorithm>
#include <string>

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long CONFIRM_DOUBLE_CLICK_MS = 300;
constexpr unsigned long HOLD_PREVIEW_MS = 250;
constexpr unsigned long READER_BACK_HOLD_ACTION_MS = 1400;
constexpr unsigned long READER_STATS_HOLD_ACTION_MS = 1100;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromSideBtn;
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool sideUsePress = SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedBack();
  const bool sidePrev = sideUsePress ? input.wasPressed(MappedInputManager::Button::PageBack)
                                     : input.wasReleased(MappedInputManager::Button::PageBack);
  const bool sideNext = sideUsePress ? input.wasPressed(MappedInputManager::Button::PageForward)
                                     : input.wasReleased(MappedInputManager::Button::PageForward);
  const bool frontPrev = input.wasReleased(MappedInputManager::Button::Left);
  const bool powerReleased = input.wasReleased(MappedInputManager::Button::Power);
  const bool shortPowerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN && powerReleased &&
                              input.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration();
  const bool longPowerTurn = SETTINGS.longPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN && powerReleased &&
                             input.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration();
  const bool frontNext = input.wasReleased(MappedInputManager::Button::Right) || shortPowerTurn || longPowerTurn;
  const bool fromSide = (sidePrev || sideNext) && !(frontPrev || frontNext);
  return {tiltPrev || sidePrev || frontPrev, tiltNext || sideNext || frontNext, fromSide, tiltPrev || tiltNext};
}

inline bool hasNonConfirmNavigationInput(const MappedInputManager& input) {
  return input.wasPressed(MappedInputManager::Button::Back) || input.wasReleased(MappedInputManager::Button::Back) ||
         input.wasPressed(MappedInputManager::Button::PageBack) ||
         input.wasReleased(MappedInputManager::Button::PageBack) ||
         input.wasPressed(MappedInputManager::Button::PageForward) ||
         input.wasReleased(MappedInputManager::Button::PageForward) ||
         input.wasPressed(MappedInputManager::Button::Left) || input.wasReleased(MappedInputManager::Button::Left) ||
         input.wasPressed(MappedInputManager::Button::Right) || input.wasReleased(MappedInputManager::Button::Right) ||
         input.wasPressed(MappedInputManager::Button::Up) || input.wasReleased(MappedInputManager::Button::Up) ||
         input.wasPressed(MappedInputManager::Button::Down) || input.wasReleased(MappedInputManager::Button::Down) ||
         input.wasPressed(MappedInputManager::Button::Power) || input.wasReleased(MappedInputManager::Button::Power);
}

inline bool registerConfirmDoubleClick(bool& waitingForSecondClick, unsigned long& firstClickMs, const unsigned long nowMs) {
  if (waitingForSecondClick && nowMs - firstClickMs <= CONFIRM_DOUBLE_CLICK_MS) {
    waitingForSecondClick = false;
    firstClickMs = 0UL;
    return true;
  }

  waitingForSecondClick = true;
  firstClickMs = nowMs;
  return false;
}

inline bool hasPendingConfirmSingleClickExpired(const bool waitingForSecondClick, const unsigned long firstClickMs,
                                                const unsigned long nowMs) {
  return waitingForSecondClick && nowMs - firstClickMs > CONFIRM_DOUBLE_CLICK_MS;
}

inline bool getConfiguredReaderRefreshMode(HalDisplay::RefreshMode& mode) {
  return SETTINGS.getForcedReaderRefreshMode(mode);
}

inline void drawHoldPreview(GfxRenderer& renderer, const char* text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int horizontalPadding = 12;
  constexpr int verticalPadding = 6;
  constexpr int radius = 6;
  const int maxTextWidth = std::max(24, pageWidth - metrics.contentSidePadding * 2 - horizontalPadding * 2);
  const std::string safeText = renderer.truncatedText(SMALL_FONT_ID, text, maxTextWidth,
                                                      EpdFontFamily::BOLD);
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, safeText.c_str(), EpdFontFamily::BOLD);
  const int pillWidth = std::min(std::max(48, pageWidth - metrics.contentSidePadding * 2),
                                 textWidth + horizontalPadding * 2);
  const int pillHeight = lineHeight + verticalPadding * 2;
  const int x = (pageWidth - pillWidth) / 2;
  const int y = std::max(metrics.topPadding,
                         renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing -
                             pillHeight - 10);
  renderer.fillRoundedRect(x, y, pillWidth, pillHeight, radius, Color::Black);
  renderer.drawRoundedRect(x, y, pillWidth, pillHeight, 1, radius, true);
  renderer.drawText(SMALL_FONT_ID, x + (pillWidth - textWidth) / 2, y + verticalPadding, safeText.c_str(), false,
                    EpdFontFamily::BOLD);
  renderer.displayBuffer();
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh,
                                    const bool forceFullRefresh = false) {
  if (forceFullRefresh) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }

  HalDisplay::RefreshMode configuredMode;
  if (getConfiguredReaderRefreshMode(configuredMode)) {
    renderer.displayBuffer(configuredMode);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }

  if (pagesUntilFullRefresh <= 1) {
    if (renderer.isDarkMode()) {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (renderer.isDarkMode()) {
    return;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

}  // namespace ReaderUtils
