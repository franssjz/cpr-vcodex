#include "TimeZoneSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"
#include "util/TimeZoneRegistry.h"

void TimeZoneSelectActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset);
  requestUpdate();
}

void TimeZoneSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    {
      RenderLock lock(*this);
      SETTINGS.timeZonePreset = TimeZoneRegistry::clampPresetIndex(static_cast<uint8_t>(selectedIndex));
      SETTINGS.markDirty();
      TimeUtils::configureTimezone();
    }
    finish();
    return;
  }

  const int totalItems = static_cast<int>(TimeZoneRegistry::getPresetCount());
  buttonNavigator.onNextRelease([this, totalItems] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });
}

void TimeZoneSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();
  const int totalItems = static_cast<int>(TimeZoneRegistry::getPresetCount());
  const int currentIndex = TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TIME_ZONE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectedIndex,
      [](int index) { return std::string(TimeZoneRegistry::getPresetLabel(static_cast<uint8_t>(index))); }, nullptr,
      nullptr,
      [currentIndex](int index) { return index == currentIndex ? std::string(tr(STR_SELECTED)) : std::string(""); },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
