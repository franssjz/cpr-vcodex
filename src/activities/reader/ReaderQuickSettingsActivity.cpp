#include "ReaderQuickSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "activities/settings/FontSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int TAB_READER = 0;
constexpr int TAB_DISPLAY = 1;
constexpr int TAB_COUNT = 2;
constexpr int READER_ITEM_COUNT = 7;
constexpr int DISPLAY_ITEM_COUNT = 6;

constexpr StrId TAB_LABELS[TAB_COUNT] = {StrId::STR_CAT_READER, StrId::STR_CAT_DISPLAY};
constexpr StrId READER_ITEM_LABELS[READER_ITEM_COUNT] = {
    StrId::STR_FONT_FAMILY, StrId::STR_FONT_SIZE,      StrId::STR_LINE_SPACING,
    StrId::STR_SCREEN_MARGIN, StrId::STR_PARA_ALIGNMENT, StrId::STR_EXTRA_SPACING,
    StrId::STR_BIONIC_READING};
constexpr StrId DISPLAY_ITEM_LABELS[DISPLAY_ITEM_COUNT] = {StrId::STR_UI_THEME, StrId::STR_DARK_MODE,
                                                           StrId::STR_TEXT_DARKNESS,
                                                           StrId::STR_READER_REFRESH_MODE, StrId::STR_IMAGES,
                                                           StrId::STR_STATUS_BAR_POSITION};

constexpr StrId FONT_FAMILY_LABELS[] = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS, StrId::STR_LEXEND};
constexpr StrId FONT_SIZE_LABELS[] = {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                                      StrId::STR_X_LARGE};
constexpr StrId LINE_SPACING_LABELS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId ALIGNMENT_LABELS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER,
                                      StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE};
constexpr StrId BIONIC_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE};
constexpr StrId THEME_LABELS[] = {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_CUSTOM,
                                  StrId::STR_THEME_LYRA_VCODEX2};
constexpr StrId DARK_MODE_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON};
constexpr StrId TEXT_DARKNESS_LABELS[] = {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK,
                                          StrId::STR_EXTRA_DARK};
constexpr StrId REFRESH_MODE_LABELS[] = {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST,
                                         StrId::STR_REFRESH_MODE_HALF, StrId::STR_REFRESH_MODE_FULL};
constexpr StrId IMAGE_LABELS[] = {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER,
                                  StrId::STR_IMAGES_SUPPRESS};
constexpr StrId STATUS_BAR_PLACEMENT_LABELS[] = {StrId::STR_BOTTOM, StrId::STR_TOP, StrId::STR_HIDE};

uint8_t wrapEnum(const uint8_t value, const int direction, const uint8_t count) {
  if (count == 0) {
    return value;
  }
  if (direction > 0) {
    return static_cast<uint8_t>((value + 1) % count);
  }
  return value == 0 ? static_cast<uint8_t>(count - 1) : static_cast<uint8_t>(value - 1);
}

std::string enumValue(const uint8_t value, const StrId* labels, const uint8_t count) {
  return I18N.get(labels[std::min<uint8_t>(value, count - 1)]);
}

std::string currentFontValue() {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    return SETTINGS.sdFontFamilyName;
  }
  return enumValue(SETTINGS.fontFamily, FONT_FAMILY_LABELS, CrossPointSettings::BUILTIN_FONT_COUNT);
}

std::string valueForIndex(const int tab, const int index) {
  if (tab == TAB_READER) {
    switch (index) {
      case 0:
        return currentFontValue();
      case 1:
        return enumValue(SETTINGS.fontSize, FONT_SIZE_LABELS, CrossPointSettings::FONT_SIZE_COUNT);
      case 2:
        return enumValue(SETTINGS.lineSpacing, LINE_SPACING_LABELS, CrossPointSettings::LINE_COMPRESSION_COUNT);
      case 3:
        return std::to_string(SETTINGS.screenMargin);
      case 4:
        return enumValue(SETTINGS.paragraphAlignment, ALIGNMENT_LABELS, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
      case 5:
        return I18N.get(DARK_MODE_LABELS[SETTINGS.extraParagraphSpacing ? 1 : 0]);
      case 6:
        return enumValue(SETTINGS.bionicReading, BIONIC_LABELS, CrossPointSettings::BIONIC_READING_MODE_COUNT);
      default:
        return "";
    }
  }

  switch (index) {
    case 0:
      return enumValue(SETTINGS.uiTheme, THEME_LABELS, CrossPointSettings::UI_THEME_COUNT);
    case 1:
      return I18N.get(DARK_MODE_LABELS[SETTINGS.darkMode ? 1 : 0]);
    case 2:
      return enumValue(SETTINGS.textDarkness, TEXT_DARKNESS_LABELS, CrossPointSettings::TEXT_DARKNESS_COUNT);
    case 3:
      return enumValue(SETTINGS.readerRefreshMode, REFRESH_MODE_LABELS,
                       CrossPointSettings::READER_REFRESH_MODE_COUNT);
    case 4:
      return enumValue(SETTINGS.imageRendering, IMAGE_LABELS, CrossPointSettings::IMAGE_RENDERING_COUNT);
    case 5:
      return enumValue(SETTINGS.statusBarPlacement, STATUS_BAR_PLACEMENT_LABELS,
                       CrossPointSettings::STATUS_BAR_PLACEMENT_COUNT);
    default:
      return "";
  }
}

StrId labelForIndex(const int tab, const int index) {
  return tab == TAB_READER ? READER_ITEM_LABELS[index] : DISPLAY_ITEM_LABELS[index];
}
}  // namespace

void ReaderQuickSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedTab = TAB_READER;
  selectedIndex = 0;
  tabFocused = true;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

int ReaderQuickSettingsActivity::currentItemCount() const {
  return selectedTab == TAB_READER ? READER_ITEM_COUNT : DISPLAY_ITEM_COUNT;
}

void ReaderQuickSettingsActivity::adjustSelected(const int direction) {
  if (selectedTab == TAB_READER) {
    switch (selectedIndex) {
      case 0:
        return;
      case 1:
        SETTINGS.fontSize = wrapEnum(SETTINGS.fontSize, direction, CrossPointSettings::FONT_SIZE_COUNT);
        break;
      case 2:
        SETTINGS.lineSpacing = wrapEnum(SETTINGS.lineSpacing, direction, CrossPointSettings::LINE_COMPRESSION_COUNT);
        break;
      case 3:
        if (direction > 0) {
          SETTINGS.screenMargin = std::min<uint8_t>(40, SETTINGS.screenMargin + 5);
        } else {
          SETTINGS.screenMargin = SETTINGS.screenMargin <= 5 ? 40 : static_cast<uint8_t>(SETTINGS.screenMargin - 5);
        }
        break;
      case 4:
        SETTINGS.paragraphAlignment =
            wrapEnum(SETTINGS.paragraphAlignment, direction, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
        break;
      case 5:
        SETTINGS.extraParagraphSpacing = SETTINGS.extraParagraphSpacing ? 0 : 1;
        break;
      case 6:
        SETTINGS.bionicReading =
            wrapEnum(SETTINGS.bionicReading, direction, CrossPointSettings::BIONIC_READING_MODE_COUNT);
        break;
      default:
        return;
    }
  } else {
    switch (selectedIndex) {
      case 0:
        SETTINGS.uiTheme = wrapEnum(SETTINGS.uiTheme, direction, CrossPointSettings::UI_THEME_COUNT);
        UITheme::getInstance().reload();
        break;
      case 1:
        SETTINGS.darkMode = SETTINGS.darkMode ? 0 : 1;
        renderer.setDarkMode(SETTINGS.darkMode);
        break;
      case 2:
        SETTINGS.textDarkness = wrapEnum(SETTINGS.textDarkness, direction, CrossPointSettings::TEXT_DARKNESS_COUNT);
        renderer.setTextDarkness(SETTINGS.textDarkness);
        break;
      case 3:
        SETTINGS.readerRefreshMode =
            wrapEnum(SETTINGS.readerRefreshMode, direction, CrossPointSettings::READER_REFRESH_MODE_COUNT);
        break;
      case 4:
        SETTINGS.imageRendering = wrapEnum(SETTINGS.imageRendering, direction, CrossPointSettings::IMAGE_RENDERING_COUNT);
        break;
      case 5:
        SETTINGS.statusBarPlacement =
            wrapEnum(SETTINGS.statusBarPlacement, direction, CrossPointSettings::STATUS_BAR_PLACEMENT_COUNT);
        break;
      default:
        return;
    }
  }
  SETTINGS.saveToFile();
}

void ReaderQuickSettingsActivity::openFontPicker() {
  startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                 FontSelectionActivity::Mode::Select),
                         [this](const ActivityResult&) {
                           SETTINGS.saveToFile();
                           sdFontSystem.ensureLoaded(renderer);
                           requestUpdate();
                         });
}

void ReaderQuickSettingsActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }
  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!tabFocused) {
      tabFocused = true;
      requestUpdate();
      return;
    }
    finish();
    return;
  }

  if (tabFocused) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedTab = selectedTab == TAB_READER ? TAB_DISPLAY : TAB_READER;
      selectedIndex = 0;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      selectedIndex = 0;
      tabFocused = false;
      requestUpdate();
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedTab == TAB_READER && selectedIndex == 0) {
      openFontPicker();
      return;
    }
    adjustSelected(1);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, currentItemCount());
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (selectedIndex <= 0) {
      selectedIndex = currentItemCount() - 1;
    } else {
      selectedIndex--;
    }
    requestUpdate();
    return;
  }
}

void ReaderQuickSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_QUICK_SETTINGS));

  const int tabsTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  std::vector<TabInfo> tabs;
  tabs.reserve(TAB_COUNT);
  for (int tab = 0; tab < TAB_COUNT; ++tab) {
    tabs.push_back(TabInfo{I18N.get(TAB_LABELS[tab]), tab == selectedTab});
  }
  GUI.drawTabBar(renderer, Rect{sidePadding, tabsTop, pageWidth - sidePadding * 2, metrics.tabBarHeight}, tabs,
                 tabFocused);

  const int contentTop = tabsTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, currentItemCount(), tabFocused ? -1 : selectedIndex,
      [this](int index) { return std::string(I18N.get(labelForIndex(selectedTab, index))); }, nullptr, nullptr,
      [this](int index) { return valueForIndex(selectedTab, index); }, true);

  auto labels = tabFocused ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                           : mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  if (!tabFocused && selectedTab == TAB_READER && selectedIndex == 0) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
