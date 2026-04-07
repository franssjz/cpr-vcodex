#include "SettingsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>

#include "AchievementsStore.h"
#include "ButtonRemapActivity.h"
#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OtaUpdateActivity.h"
#include "ReadingStatsStore.h"
#include "SettingsList.h"
#include "ShortcutLocationActivity.h"
#include "ShortcutOrderActivity.h"
#include "ShortcutVisibilityActivity.h"
#include "StatusBarSettingsActivity.h"
#include "TimeZoneSelectActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {
    StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER, StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM, StrId::STR_APPS};

namespace {
std::string getReadingStatsExportPath() {
  constexpr char defaultPath[] = "/exports/reading_stats_export.json";

  const time_t now = time(nullptr);
  if (!TimeUtils::isClockValid(static_cast<uint32_t>(now))) {
    return defaultPath;
  }

  tm localTime = {};
  if (localtime_r(&now, &localTime) == nullptr) {
    return defaultPath;
  }

  char buffer[96];
  snprintf(buffer, sizeof(buffer), "/exports/reading_stats_%04d%02d%02d_%02d%02d%02d.json", localTime.tm_year + 1900,
           localTime.tm_mon + 1, localTime.tm_mday, localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  return buffer;
}

bool startsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string getLatestReadingStatsImportPath() {
  auto exportsDir = Storage.open("/exports");
  if (!exportsDir || !exportsDir.isDirectory()) {
    if (exportsDir) {
      exportsDir.close();
    }
    return "";
  }

  exportsDir.rewindDirectory();

  std::string latestTimestamped;
  std::string fallbackPath;
  char name[256];
  for (auto entry = exportsDir.openNextFile(); entry; entry = exportsDir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    const std::string fileName{name};
    if (!startsWith(fileName, "reading_stats") || !endsWith(fileName, ".json")) {
      entry.close();
      continue;
    }

    const std::string fullPath = "/exports/" + fileName;
    if (startsWith(fileName, "reading_stats_20")) {
      latestTimestamped = std::max(latestTimestamped, fullPath);
    } else if (fallbackPath.empty()) {
      fallbackPath = fullPath;
    }
    entry.close();
  }
  exportsDir.close();

  return latestTimestamped.empty() ? fallbackPath : latestTimestamped;
}

std::string getSettingValueText(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool value = SETTINGS.*(setting.valuePtr);
    return value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  if (setting.type == SettingType::ACTION && setting.action == SettingAction::TimeZone) {
    return TimeUtils::getCurrentTimeZoneLabel();
  }
  return "";
}

const char* getSettingNameText(const SettingInfo& setting) { return I18N.get(setting.nameId); }
}  // namespace

void SettingsActivity::onEnter() {
  Activity::onEnter();

  buildSettingsLists();

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;

  // Initialize with first category (Display)
  enterCategory(0);

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::buildSettingsLists() {
  if (settingsListsBuilt) {
    return;
  }

  // Build per-category vectors from the shared settings list once.
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  appSettings.clear();

  const auto& sharedSettings = getSettingsList();
  displaySettings.reserve(sharedSettings.size());
  readerSettings.reserve(sharedSettings.size());
  controlsSettings.reserve(sharedSettings.size());
  systemSettings.reserve(sharedSettings.size());
  appSettings.reserve(sharedSettings.size() + 16);

  for (const auto& setting : sharedSettings) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    } else if (setting.category == StrId::STR_APPS) {
      continue;
    }
    // Web-only categories (KOReader Sync, OPDS Browser) are skipped for device UI
  }

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_BROWSER, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  appSettings.push_back(SettingInfo::Section(StrId::STR_DAY_SYNC_SECTION));
  appSettings.push_back(
      SettingInfo::Toggle(StrId::STR_DISPLAY_DAY, &CrossPointSettings::displayDay, "displayDay", StrId::STR_APPS));
  appSettings.push_back(
      SettingInfo::Toggle(StrId::STR_AUTO_SYNC_DAY, &CrossPointSettings::autoSyncDay, "autoSyncDay", StrId::STR_APPS));
  appSettings.push_back(SettingInfo::Enum(
      StrId::STR_DATE_FORMAT, &CrossPointSettings::dateFormat,
      {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY, StrId::STR_DATE_FORMAT_YYYY_MM_DD},
      "dateFormat", StrId::STR_APPS));
  appSettings.push_back(SettingInfo::Action(StrId::STR_TIME_ZONE, SettingAction::TimeZone));
  appSettings.push_back(SettingInfo::Section(StrId::STR_READING_STATS));
  appSettings.push_back(SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                                          {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60},
                                          "dailyGoalTarget", StrId::STR_APPS));
  appSettings.push_back(SettingInfo::Toggle(StrId::STR_SHOW_AFTER_READING, &CrossPointSettings::showStatsAfterReading,
                                            "showStatsAfterReading", StrId::STR_APPS));
  appSettings.push_back(SettingInfo::Action(StrId::STR_RESET, SettingAction::ResetReadingStats));
  appSettings.push_back(SettingInfo::Action(StrId::STR_EXPORT, SettingAction::ExportReadingStats));
  appSettings.push_back(SettingInfo::Action(StrId::STR_IMPORT, SettingAction::ImportReadingStats));
  appSettings.push_back(SettingInfo::Section(StrId::STR_ACHIEVEMENTS));
  appSettings.push_back(SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled,
                                            "achievementsEnabled", StrId::STR_APPS));
  appSettings.push_back(SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups,
                                            "achievementPopups", StrId::STR_APPS));
  appSettings.push_back(SettingInfo::Action(StrId::STR_RESET_ACHIEVEMENTS, SettingAction::ResetAchievements));
  appSettings.push_back(SettingInfo::Action(StrId::STR_SYNC_WITH_PREV_STATS, SettingAction::SyncAchievementsFromStats));
  appSettings.push_back(SettingInfo::Section(StrId::STR_SHORTCUTS_SECTION));
  appSettings.push_back(SettingInfo::Action(StrId::STR_SHORTCUT_LOCATION, SettingAction::ShortcutLocation));
  appSettings.push_back(SettingInfo::Action(StrId::STR_SHORTCUT_VISIBILITY, SettingAction::ShortcutVisibility));
  appSettings.push_back(SettingInfo::Action(StrId::STR_ORDER_HOME_SHORTCUTS, SettingAction::OrderHomeShortcuts));
  appSettings.push_back(SettingInfo::Action(StrId::STR_ORDER_APPS_SHORTCUTS, SettingAction::OrderAppsShortcuts));
  settingsListsBuilt = true;
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::enterCategory(const int categoryIndex) {
  selectedCategoryIndex = categoryIndex;
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
    default:
      currentSettings = &appSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

bool SettingsActivity::isSelectableSetting(const int settingIndex) const {
  if (!currentSettings || settingIndex < 0 || settingIndex >= settingsCount) {
    return false;
  }
  return (*currentSettings)[settingIndex].type != SettingType::SECTION;
}

int SettingsActivity::firstSelectableSettingIndex() const {
  for (int index = 0; index < settingsCount; ++index) {
    if (isSelectableSetting(index)) {
      return index + 1;
    }
  }
  return 0;
}

int SettingsActivity::stepSettingSelection(const int direction) const {
  const int totalSlots = settingsCount + 1;
  if (totalSlots <= 1) {
    return 0;
  }

  int candidate = selectedSettingIndex;
  for (int guard = 0; guard < totalSlots; ++guard) {
    candidate = direction > 0 ? ButtonNavigator::nextIndex(candidate, totalSlots)
                              : ButtonNavigator::previousIndex(candidate, totalSlots);
    if (candidate == 0 || isSelectableSetting(candidate - 1)) {
      return candidate;
    }
  }

  return selectedSettingIndex;
}

void SettingsActivity::showTransientPopup(const char* message, const int progress, const unsigned long delayMs) {
  requestUpdateAndWait();

  {
    RenderLock lock(*this);
    const Rect popupRect = GUI.drawPopup(renderer, message);
    if (progress >= 0) {
      GUI.fillPopupProgress(renderer, popupRect, progress);
    }
  }

  if (delayMs > 0) {
    delay(delayMs);
  }
}

void SettingsActivity::loop() {
  bool hasChangedCategory = false;

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = stepSettingSelection(1);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = stepSettingSelection(-1);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  if (hasChangedCategory) {
    enterCategory(selectedCategoryIndex);
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : firstSelectableSettingIndex();
  }
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<CalibreSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::TimeZone:
        startActivityForResult(std::make_unique<TimeZoneSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ShortcutLocation:
        startActivityForResult(std::make_unique<ShortcutLocationActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ShortcutVisibility:
        startActivityForResult(std::make_unique<ShortcutVisibilityActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OrderHomeShortcuts:
        startActivityForResult(std::make_unique<ShortcutOrderActivity>(renderer, mappedInput, ShortcutOrderGroup::Home),
                               resultHandler);
        break;
      case SettingAction::OrderAppsShortcuts:
        startActivityForResult(std::make_unique<ShortcutOrderActivity>(renderer, mappedInput, ShortcutOrderGroup::Apps),
                               resultHandler);
        break;
      case SettingAction::ResetReadingStats:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_READING_STATS_CONFIRM), ""),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                RenderLock lock(*this);
                READING_STATS.reset();
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::ExportReadingStats: {
        showTransientPopup(tr(STR_EXPORTING), 20, 120);
        Storage.mkdir("/exports");
        const bool exported = READING_STATS.exportToFile(getReadingStatsExportPath());
        showTransientPopup(exported ? tr(STR_EXPORT_DONE) : tr(STR_EXPORT_FAILED), exported ? 100 : -1,
                           exported ? 350 : 700);
        requestUpdate(true);
        break;
      }
      case SettingAction::ImportReadingStats:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_IMPORT_READING_STATS_CONFIRM), ""),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const std::string importPath = getLatestReadingStatsImportPath();
                if (importPath.empty()) {
                  showTransientPopup(tr(STR_NO_READING_STATS_EXPORT), -1, 700);
                } else {
                  showTransientPopup(tr(STR_IMPORTING), 20, 120);
                  const bool imported = READING_STATS.importFromFile(importPath);
                  showTransientPopup(imported ? tr(STR_IMPORT_DONE) : tr(STR_IMPORT_FAILED), imported ? 100 : -1,
                                     imported ? 350 : 700);
                }
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::ResetAchievements:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_ACHIEVEMENTS_CONFIRM), ""),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                ACHIEVEMENTS.reset();
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::SyncAchievementsFromStats:
        showTransientPopup(tr(STR_SYNC_WITH_PREV_STATS), 20, 120);
        ACHIEVEMENTS.syncWithPreviousStats();
        showTransientPopup(tr(STR_DONE), 100, 350);
        requestUpdate(true);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  if (setting.valuePtr == &CrossPointSettings::dailyGoalTarget) {
    ACHIEVEMENTS.syncWithPreviousStats();
  }

  if (setting.valuePtr == &CrossPointSettings::darkMode) {
    renderer.setDarkMode(SETTINGS.darkMode);
    renderer.requestNextFullRefresh();
    requestUpdate(true);
  }

  SETTINGS.saveToFile();
}

void SettingsActivity::renderAppSettingsList(const Rect& rect) const {
  if (!currentSettings) {
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& settings = *currentSettings;
  if (settings.empty() || rect.height <= 0) {
    return;
  }

  const int rowHeight = metrics.listRowHeight;
  const int sectionHeight = 40;
  const int sidePadding = metrics.contentSidePadding;
  constexpr int scrollBarWidth = 4;
  constexpr int scrollBarGap = 6;
  const int rowX = rect.x + sidePadding;
  const int rowWidth = rect.width - sidePadding * 2 - scrollBarWidth - scrollBarGap;
  const int viewportHeight = rect.height;

  auto getItemHeight = [rowHeight, sectionHeight](const SettingInfo& setting) {
    return setting.type == SettingType::SECTION ? sectionHeight : rowHeight;
  };

  std::vector<int> itemOffsets(settingsCount, 0);
  int totalHeight = 0;
  for (int index = 0; index < settingsCount; ++index) {
    itemOffsets[index] = totalHeight;
    totalHeight += getItemHeight(settings[index]);
  }

  int firstVisibleIndex = 0;
  int visibleWindowHeight = 0;
  if (selectedSettingIndex > 0) {
    const int selectedIndex = std::clamp(selectedSettingIndex - 1, 0, settingsCount - 1);
    for (int index = 0; index <= selectedIndex; ++index) {
      visibleWindowHeight += getItemHeight(settings[index]);
      while (visibleWindowHeight > viewportHeight && firstVisibleIndex <= index) {
        visibleWindowHeight -= getItemHeight(settings[firstVisibleIndex]);
        ++firstVisibleIndex;
      }
    }

    if (firstVisibleIndex > 0 && settings[firstVisibleIndex - 1].type == SettingType::SECTION) {
      const int headerHeight = getItemHeight(settings[firstVisibleIndex - 1]);
      if (visibleWindowHeight + headerHeight <= viewportHeight) {
        --firstVisibleIndex;
        visibleWindowHeight += headerHeight;
      }
    }
  }

  int currentY = rect.y;
  int renderedHeight = 0;

  for (int index = firstVisibleIndex; index < settingsCount; ++index) {
    const auto& setting = settings[index];
    const int itemHeight = getItemHeight(setting);
    if (renderedHeight + itemHeight > viewportHeight) {
      break;
    }

    if (setting.type == SettingType::SECTION) {
      const char* label = I18N.get(setting.nameId);
      renderer.drawText(UI_10_FONT_ID, rowX, currentY + 4, label, true, EpdFontFamily::BOLD);
      renderer.drawLine(rowX, currentY + itemHeight - 5, rowX + rowWidth, currentY + itemHeight - 5);
      currentY += itemHeight;
      renderedHeight += itemHeight;
      continue;
    }

    const bool selected = selectedSettingIndex == index + 1;
    const Rect rowRect{rowX, currentY, rowWidth, itemHeight - 4};
    if (selected) {
      renderer.fillRectDither(rowRect.x, rowRect.y, rowRect.width, rowRect.height, Color::LightGray);
      renderer.drawRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height);
    }

    const std::string valueText = getSettingValueText(setting);
    const int valueWidth =
        valueText.empty() ? 0 : renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str(), EpdFontFamily::REGULAR);
    const int leftPadding = 12;
    const int rightPadding = 12;
    const int labelWidth = rowRect.width - leftPadding - rightPadding - (valueWidth > 0 ? valueWidth + 12 : 0);
    const std::string titleText =
        renderer.truncatedText(UI_10_FONT_ID, getSettingNameText(setting), labelWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rowRect.x + leftPadding, rowRect.y + 9, titleText.c_str(), true,
                      EpdFontFamily::REGULAR);

    if (!valueText.empty()) {
      renderer.drawText(UI_10_FONT_ID, rowRect.x + rowRect.width - rightPadding - valueWidth, rowRect.y + 9,
                        valueText.c_str(), true, EpdFontFamily::REGULAR);
    }

    currentY += itemHeight;
    renderedHeight += itemHeight;
  }

  if (totalHeight > viewportHeight) {
    const int scrollTrackX = rect.x + rect.width - sidePadding;
    const int scrollOffset = itemOffsets[firstVisibleIndex];
    const int scrollBarHeight = std::max(18, (viewportHeight * viewportHeight) / totalHeight);
    const int maxScrollOffset = std::max(1, totalHeight - viewportHeight);
    const int scrollBarY =
        rect.y + ((viewportHeight - scrollBarHeight) * std::min(scrollOffset, maxScrollOffset)) / maxScrollOffset;

    renderer.drawLine(scrollTrackX, rect.y, scrollTrackX, rect.y + viewportHeight, true);
    renderer.fillRect(scrollTrackX - scrollBarWidth + 1, scrollBarY, scrollBarWidth, scrollBarHeight, true);
  }
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  constexpr int listBottomGap = 10;
  const Rect listRect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing,
                      pageWidth,
                      pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                                    metrics.buttonHintsHeight + metrics.verticalSpacing * 2 + listBottomGap)};

  if (selectedCategoryIndex == 4) {
    renderAppSettingsList(listRect);
  } else {
    const auto& settings = *currentSettings;
    GUI.drawList(
        renderer, listRect, settingsCount, selectedSettingIndex - 1,
        [&settings](int index) { return std::string(getSettingNameText(settings[index])); }, nullptr, nullptr,
        [&settings](int i) { return getSettingValueText(settings[i]); }, true);
  }

  // Draw help text
  const char* confirmLabel = nullptr;
  if (selectedSettingIndex == 0) {
    confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
  } else {
    const auto& selectedSetting = (*currentSettings)[selectedSettingIndex - 1];
    confirmLabel = (selectedSetting.type == SettingType::ACTION || selectedSetting.type == SettingType::SECTION)
                       ? tr(STR_SELECT)
                       : tr(STR_TOGGLE);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
