#include "ReaderQuickSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ITEM_COUNT = 5;
constexpr StrId ITEM_LABELS[ITEM_COUNT] = {StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING, StrId::STR_SCREEN_MARGIN,
                                           StrId::STR_UI_THEME, StrId::STR_ESTIMATED_TIME_LEFT};
constexpr StrId FONT_SIZE_LABELS[] = {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                                      StrId::STR_X_LARGE};
constexpr StrId LINE_SPACING_LABELS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId THEME_LABELS[] = {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_CUSTOM};
constexpr StrId TIME_LEFT_LABELS[] = {StrId::STR_HIDE, StrId::STR_BOOK, StrId::STR_CHAPTER};

uint8_t wrapEnum(const uint8_t value, const int direction, const uint8_t count) {
  if (count == 0) {
    return value;
  }
  if (direction > 0) {
    return static_cast<uint8_t>((value + 1) % count);
  }
  return value == 0 ? static_cast<uint8_t>(count - 1) : static_cast<uint8_t>(value - 1);
}

std::string valueForIndex(const int index) {
  switch (index) {
    case 0:
      return I18N.get(FONT_SIZE_LABELS[std::min<uint8_t>(SETTINGS.fontSize, CrossPointSettings::FONT_SIZE_COUNT - 1)]);
    case 1:
      return I18N.get(
          LINE_SPACING_LABELS[std::min<uint8_t>(SETTINGS.lineSpacing, CrossPointSettings::LINE_COMPRESSION_COUNT - 1)]);
    case 2:
      return std::to_string(SETTINGS.screenMargin);
    case 3:
      return I18N.get(THEME_LABELS[std::min<uint8_t>(SETTINGS.uiTheme, CrossPointSettings::UI_THEME_COUNT - 1)]);
    case 4:
      return I18N.get(
          TIME_LEFT_LABELS[std::min<uint8_t>(SETTINGS.statusBarTimeLeft,
                                             CrossPointSettings::STATUS_BAR_TIME_LEFT_COUNT - 1)]);
    default:
      return "";
  }
}
}  // namespace

void ReaderQuickSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void ReaderQuickSettingsActivity::adjustSelected(const int direction) {
  switch (selectedIndex) {
    case 0:
      SETTINGS.fontSize = wrapEnum(SETTINGS.fontSize, direction, CrossPointSettings::FONT_SIZE_COUNT);
      break;
    case 1:
      SETTINGS.lineSpacing = wrapEnum(SETTINGS.lineSpacing, direction, CrossPointSettings::LINE_COMPRESSION_COUNT);
      break;
    case 2:
      if (direction > 0) {
        SETTINGS.screenMargin = std::min<uint8_t>(40, SETTINGS.screenMargin + 5);
      } else {
        SETTINGS.screenMargin = SETTINGS.screenMargin <= 5 ? 40 : static_cast<uint8_t>(SETTINGS.screenMargin - 5);
      }
      break;
    case 3:
      SETTINGS.uiTheme = wrapEnum(SETTINGS.uiTheme, direction, CrossPointSettings::UI_THEME_COUNT);
      UITheme::getInstance().reload();
      break;
    case 4:
      SETTINGS.statusBarTimeLeft =
          wrapEnum(SETTINGS.statusBarTimeLeft, direction, CrossPointSettings::STATUS_BAR_TIME_LEFT_COUNT);
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

void ReaderQuickSettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    adjustSelected(1);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    adjustSelected(-1);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
}

void ReaderQuickSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_QUICK_SETTINGS));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex,
      [](int index) { return std::string(I18N.get(ITEM_LABELS[index])); }, nullptr, nullptr,
      [](int index) { return valueForIndex(index); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
