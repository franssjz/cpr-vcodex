#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <components/bars/tap-zones.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <new>
#include <string_view>

#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long CONFIRM_DOUBLE_CLICK_MS = 300;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline bool gestureAllowsSwipe(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::SWIPE_ONLY;
}

inline bool gestureAllowsTap(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY ||
         gesture == CrossPointSettings::INVERTED_TAP;
}

inline bool isRtlBookLanguage(std::string_view tag) {
  if (tag.size() < 2 || (tag.size() > 2 && tag[2] != '-' && tag[2] != '_')) return false;
  const auto first = std::tolower(static_cast<unsigned char>(tag[0]));
  const auto second = std::tolower(static_cast<unsigned char>(tag[1]));
  return (first == 'h' && second == 'e') || (first == 'i' && second == 'w') || (first == 'a' && second == 'r') ||
         (first == 'f' && second == 'a');
}

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

// Front/side/power/tilt page-turn detection. The front button swap follows the
// live rendered orientation through MappedInputManager::isNavDirectionSwapped(),
// which already honours the fork's frontButtonFollowOrientation toggle on
// button-only boards.
inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedBack();
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const auto pageButtonTriggered = [&](const MappedInputManager::Button button) {
    if (usePress) return input.wasPressed(button);
    return input.wasLongPressed(button, SKIP_HOLD_MS) || input.wasReleased(button);
  };
  const bool prev =
      tiltPrev || (pageButtonTriggered(MappedInputManager::Button::PageBack) || pageButtonTriggered(prevButton));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = input.homeButtonAction() == HomeButtonAction::NextPage || tiltNext ||
                    pageButtonTriggered(MappedInputManager::Button::PageForward) || powerTurn ||
                    pageButtonTriggered(nextButton);
  return {prev, next, tiltPrev || tiltNext};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};

inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input,
                                         const bool rtlBook = false) {
  TouchPageTurn result{false, false, 0};
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  // A slow swipe never becomes a long-press chapter skip.
  const auto dir = input.wasSwipe();
  if (dir != MappedInputManager::SwipeDir::None) {
    result.next = dir == (rtlBook ? MappedInputManager::SwipeDir::Right : MappedInputManager::SwipeDir::Left) &&
                  gestureAllowsSwipe(SETTINGS.pageTurnGesture);
    result.prev = dir == (rtlBook ? MappedInputManager::SwipeDir::Left : MappedInputManager::SwipeDir::Right) &&
                  gestureAllowsSwipe(SETTINGS.previousPageGesture);
    return result;
  }

  const bool nextTaps = gestureAllowsTap(SETTINGS.pageTurnGesture);
  const bool prevTaps = gestureAllowsTap(SETTINGS.previousPageGesture);
  if (!nextTaps && !prevTaps) {
    return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return result;
  }

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  // The centered reader-menu tap target (isTouchMenuTap below) keeps priority
  // over the page-turn zones.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_TAP && x >= width / 3 && x < width - width / 3 &&
      y >= height / 3 && y < height - height / 3) {
    return result;
  }

  // Give the whole page to the sole tap-enabled direction. When both accept
  // taps, split at the left third. RTL books and Inverted Tap each reverse
  // the shared zones.
  const bool inverted = (SETTINGS.pageTurnGesture == CrossPointSettings::INVERTED_TAP ||
                         SETTINGS.previousPageGesture == CrossPointSettings::INVERTED_TAP) != rtlBook;
  const bool nextZone = inverted ? x < (width * 2) / 3 : x >= width / 3;
  result.next = nextTaps && (!prevTaps || nextZone);
  result.prev = prevTaps && (!nextTaps || !nextZone);
  result.heldMs = gpio.lastTouchHeldMs();
  return result;
}

// Tap in the center third of the screen: the tap path into the reader menu on
// every touch board. detectTouchPageTurn() excludes this centered rectangle,
// so it remains free in tap mode. The Off/Swipe Up
// alternatives are only surfaced on home-key boards (SettingsList), where the
// menu stays reachable through the key's long-press function.
inline bool isTouchMenuTap(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return false;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int zoneWidth = width / 3;
  const int zoneHeight = height / 3;
  return x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight;
}

// Reader menu opens on the menu edge-swipe or a center-third tap. Home-key
// actions are configured separately from screen gestures.
// Menu gestures honor showReaderMenu independently of touchReaderControls,
// which only gates page-turn touch zones in detectTouchPageTurn().
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (input.wasMenuGesture()) return true;
  // Bottom-edge up-swipe variant: only selectable on home-key boards, where
  // Home is the capacitive key and the bottom edge is otherwise unused.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_SWIPE_UP && input.wasReaderMenuSwipeUp()) {
    return true;
  }
  return isTouchMenuTap(renderer, input);
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

inline bool registerConfirmDoubleClick(bool& waitingForSecondClick, unsigned long& firstClickMs,
                                       const unsigned long nowMs) {
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

// Refresh mode for the next page display given the refresh-cycle counter: the
// user-forced reader mode wins, then the periodic clean refresh (FAST in dark
// mode, where a HALF refresh flashes white), else a plain fast refresh.
inline HalDisplay::RefreshMode nextPageRefreshMode(const GfxRenderer& renderer, const int pagesUntilFullRefresh) {
  HalDisplay::RefreshMode configuredMode;
  if (getConfiguredReaderRefreshMode(configuredMode)) {
    return configuredMode;
  }
  if (pagesUntilFullRefresh <= 1) {
    return renderer.isDarkMode() ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH;
  }
  return HalDisplay::FAST_REFRESH;
}

inline void advanceRefreshCycle(int& pagesUntilFullRefresh) {
  HalDisplay::RefreshMode configuredMode;
  if (pagesUntilFullRefresh <= 1 || getConfiguredReaderRefreshMode(configuredMode)) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
// forceFullRefresh (fork: double-click Select / forced refresh) always drives a
// FULL refresh and restarts the cycle.
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh,
                                    const bool forceFullRefresh = false, const bool async = false) {
  if (forceFullRefresh) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }

  const auto mode = nextPageRefreshMode(renderer, pagesUntilFullRefresh);
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  advanceRefreshCycle(pagesUntilFullRefresh);
}

// Display the B/W base of a page whose grayscale pass follows. Panels that
// combine the base (Paper Mono) defer the activation so base + gray planes go
// out as one waveform — displaying the base separately makes the gray pass
// re-drive the whole text body (a visible flash). Other panels display
// normally. Same refresh-cadence bookkeeping as displayWithRefreshCycle.
inline void displayBaseWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh,
                                        const bool forceFullRefresh = false) {
  if (renderer.grayscaleCapabilities().base != HalDisplay::GrayscaleBase::Combined) {
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh, forceFullRefresh);
    return;
  }
  if (forceFullRefresh) {
    renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }
  renderer.displayGrayscaleBase(nextPageRefreshMode(renderer, pagesUntilFullRefresh));
  advanceRefreshCycle(pagesUntilFullRefresh);
}

inline void requestReaderUiTransitionRefresh(GfxRenderer& renderer) {
  if (SETTINGS.darkMode || renderer.isDarkMode()) {
    return;
  }

  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
}

// Strip-tiled grayscale pass (fork): renders the LSB/MSB planes 80 rows at a
// time through a small scratch buffer instead of a second full framebuffer.
// Returns false when the renderer lacks strip support or the scratch cannot be
// allocated, in which case the caller falls back to the BW-snapshot path.
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
  LOG_DBG(tag, "Tiled grayscale RAM: scratch=%d free=%u->%u->%u maxAlloc=%u->%u->%u", displayWidthBytes * STRIP_ROWS,
          heapBefore.freeHeap, heapAfterAlloc.freeHeap, heapAfter.freeHeap, heapBefore.maxAllocHeap,
          heapAfterAlloc.maxAllocHeap, heapAfter.maxAllocHeap);
  return true;
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
// Dark mode skips the pass entirely (fork); the strip-tiled path is preferred
// and the full BW-snapshot path is the fallback.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (renderer.isDarkMode()) {
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined)
      renderer.cleanupGrayscaleWithFrameBuffer();
    return;
  }

  if (renderTiledGrayscale(renderer, "READER", renderFn)) {
    return;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined)
      renderer.cleanupGrayscaleWithFrameBuffer();
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

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

// Returns true if the back button was consumed (caller should return).
// Long press (>= GO_BACK_OR_HOME_MS):
// - default: go to file browser
// - with backShortToFileBrowser: go home
// Short press (< GO_BACK_OR_HOME_MS):
// - default: go home
// - with backShortToFileBrowser: go to file browser.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, BackNavCallback goHome) {
  // The reading surface deliberately has no left-edge swipe-to-exit path: in
  // swipe page-turn mode a right swipe must page back instead. Home remains
  // available through the board's dedicated Home gesture/key. Back swipes stay
  // available in menus and other activities; only this reader-surface handler
  // ignores them. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
  if (mappedInput.wasBackGesture()) {
    return false;
  }

  const bool backTriggered = mappedInput.wasLongPressed(MappedInputManager::Button::Back, GO_BACK_OR_HOME_MS) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;

  const bool longPress = mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS;
  if (longPress != SETTINGS.backShortToFileBrowser) {
    activityManager.goToFileBrowser(filePath);
  } else {
    goHome.fn(goHome.ctx);
  }
  return true;
}

}  // namespace ReaderUtils
