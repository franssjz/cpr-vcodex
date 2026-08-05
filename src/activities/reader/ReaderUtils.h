#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "MappedInputManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long CONFIRM_DOUBLE_CLICK_MS = 300;
constexpr unsigned long SKIP_HOLD_MS = 700;

struct TiledGrayscaleTimings {
  uint32_t grayLsb = 0;
  uint32_t grayMsb = 0;
  uint32_t grayDisplay = 0;
  uint32_t cleanup = 0;
};

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
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedBack();
  const bool swapFront =
      SETTINGS.frontButtonFollowOrientation && (SETTINGS.orientation == CrossPointSettings::INVERTED ||
                                                SETTINGS.orientation == CrossPointSettings::LANDSCAPE_CCW);
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev = usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton))
                             : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                input.wasReleased(prevButton));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasPressed(nextButton))
                             : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasReleased(nextButton));
  return {tiltPrev || prev, tiltNext || next, tiltPrev || tiltNext};
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

inline bool shouldToggleStatusBar(const MappedInputManager& input) {
  return SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::TOGGLE_STATUS_BAR &&
         input.wasReleased(MappedInputManager::Button::Power);
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

inline void requestReaderUiTransitionRefresh(GfxRenderer& renderer) {
  if (SETTINGS.darkMode || renderer.isDarkMode()) {
    return;
  }

  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
bool renderTiledGrayscale(GfxRenderer& renderer, const char* tag, RenderFn&& renderFn,
                          TiledGrayscaleTimings* timings = nullptr) {
  if (!renderer.supportsStripGrayscale()) {
    return false;
  }

  constexpr int STRIP_ROWS = 80;
  const int displayHeight = renderer.getDisplayHeight();
  const int displayWidthBytes = renderer.getDisplayWidthBytes();
  const auto heapBefore = MemoryBudget::snapshot();
  auto scratch =
      std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[static_cast<size_t>(displayWidthBytes) * STRIP_ROWS]);
  const auto heapAfterAlloc = MemoryBudget::snapshot();
  if (!scratch) {
    LOG_ERR(tag, "OOM: grayscale strip scratch (%d bytes); falling back to BW snapshot",
            displayWidthBytes * STRIP_ROWS);
    return false;
  }

  auto renderPlane = [&](const GfxRenderer::RenderMode mode, const bool lsbPlane) {
    renderer.setRenderMode(mode);
    for (int y = 0; y < displayHeight; y += STRIP_ROWS) {
      const int rows = std::min(STRIP_ROWS, displayHeight - y);
      {
        GfxStripTargetScope strip(renderer, scratch.get(), y, rows);
        renderer.clearScreen(0x00);
        renderFn();
      }
      renderer.writeGrayscalePlaneStrip(lsbPlane, scratch.get(), y, rows);
    }
  };

  renderPlane(GfxRenderer::GRAYSCALE_LSB, true);
  const uint32_t tGrayLsb = millis();

  renderPlane(GfxRenderer::GRAYSCALE_MSB, false);
  const uint32_t tGrayMsb = millis();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayBuffer();
  const uint32_t tGrayDisplay = millis();
  renderer.cleanupGrayscaleWithFrameBuffer();
  const uint32_t tCleanup = millis();

  if (timings) {
    timings->grayLsb = tGrayLsb;
    timings->grayMsb = tGrayMsb;
    timings->grayDisplay = tGrayDisplay;
    timings->cleanup = tCleanup;
  }

  const auto heapAfter = MemoryBudget::snapshot();
  LOG_DBG(tag, "Tiled grayscale RAM: scratch=%d free=%u->%u->%u maxAlloc=%u->%u->%u",
          displayWidthBytes * STRIP_ROWS, heapBefore.freeHeap, heapAfterAlloc.freeHeap, heapAfter.freeHeap,
          heapBefore.maxAllocHeap, heapAfterAlloc.maxAllocHeap, heapAfter.maxAllocHeap);
  return true;
}

template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (renderer.isDarkMode()) {
    return;
  }

  if (renderTiledGrayscale(renderer, "READER", renderFn)) {
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

// Compact chapter-ETA duration / remaining-time helpers (defined in ReaderUtils.cpp).
bool formatCompactDuration(uint64_t totalMs, char* buf, size_t bufSize);
bool formatRemainingFromRate(uint32_t remainingPages, double pagesPerMs, char* buf, size_t bufSize);

}  // namespace ReaderUtils
