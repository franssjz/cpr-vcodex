#include "SettingsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>

#include "AchievementsStore.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "KOReaderSettingsActivity.h"
#include "FontSelectionActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "ReadingStatsStore.h"
#include "SettingsList.h"
#include "ShortcutLocationActivity.h"
#include "ShortcutOrderActivity.h"
#include "ShortcutVisibilityActivity.h"
#include "StatusBarSettingsActivity.h"
#include "TimeZoneSelectActivity.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "SdCardFontGlobals.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"
#include "util/SleepImageUtils.h"
#include "util/TimeUtils.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {
    StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER, StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM, StrId::STR_APPS};

namespace {
constexpr size_t SETTINGS_TAB_MAX_CHARS = 10;
constexpr StrId POWER_ACTION_LABELS[] = {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_FORCE_REFRESH,
                                         StrId::STR_SCREENSHOT_BUTTON, StrId::STR_AUTO_TURN_PAGES_PER_MIN};
constexpr uint8_t POWER_ACTION_VALUES[] = {CrossPointSettings::IGNORE, CrossPointSettings::SLEEP,
                                           CrossPointSettings::FORCE_REFRESH, CrossPointSettings::SCREENSHOT,
                                           CrossPointSettings::CYCLE_PAGE_TURN};
constexpr StrId MENU_ACTION_LABELS[] = {StrId::STR_IGNORE,
                                        StrId::STR_BIONIC_READING,
                                        StrId::STR_FONT_FAMILY,
                                        StrId::STR_BOOKMARKS,
                                        StrId::STR_SYNC_PROGRESS,
                                        StrId::STR_MARK_FINISHED,
                                        StrId::STR_READING_STATS};
constexpr uint8_t MENU_ACTION_VALUES[] = {CrossPointSettings::LONG_MENU_OFF,
                                          CrossPointSettings::LONG_MENU_TOGGLE_BIONIC,
                                          CrossPointSettings::LONG_MENU_CHANGE_FONT,
                                          CrossPointSettings::LONG_MENU_TOGGLE_BOOKMARK,
                                          CrossPointSettings::LONG_MENU_SYNC_PROGRESS,
                                          CrossPointSettings::LONG_MENU_MARK_FINISHED,
                                          CrossPointSettings::LONG_MENU_READING_STATS};
constexpr StrId FRONT_LONG_PRESS_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_LONG_PRESS_SKIP,
                                             StrId::STR_ORIENTATION};
constexpr StrId SIDE_LONG_PRESS_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_LONG_PRESS_SKIP, StrId::STR_ORIENTATION,
                                            StrId::STR_FONT_SIZE};
constexpr StrId ORIENTATION_LABELS[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                        StrId::STR_LANDSCAPE_CCW, StrId::STR_CYCLE_ORIENTATIONS};

template <size_t N>
std::vector<StrId> valuesFromArray(const StrId (&values)[N]) {
  return std::vector<StrId>(std::begin(values), std::end(values));
}

template <size_t N>
std::vector<uint8_t> rawValuesFromArray(const uint8_t (&values)[N]) {
  return std::vector<uint8_t>(std::begin(values), std::end(values));
}

const std::vector<SettingInfo>& getDeviceDisplaySettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                         StrId::STR_COVER_CUSTOM}),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                        {StrId::STR_FIT, StrId::STR_CROP}),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED}),
      SettingInfo::Enum(StrId::STR_SLEEP_REFRESH, &CrossPointSettings::sleepRefreshMode,
                        {StrId::STR_STATE_OFF, StrId::STR_SLEEP_REFRESH_SOFT, StrId::STR_SLEEP_REFRESH_FULL}),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}),
      SettingInfo::Enum(
          StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30}),
      SettingInfo::Enum(StrId::STR_MENU_RECENT_BOOKS, &CrossPointSettings::recentBooksView,
                        {StrId::STR_FILE_VIEW_LIST, StrId::STR_FILE_VIEW_GRID}),
      SettingInfo::Toggle(StrId::STR_SHOW_CURRENT_BOOK_CARD, &CrossPointSettings::showCurrentBookCard),
      SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode),
      SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceReaderSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Action(StrId::STR_FONT_FAMILY, SettingAction::FontSelection),
      SettingInfo::Action(StrId::STR_FONT_MANAGER, SettingAction::FontSelection),
      SettingInfo::Enum(
          StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
          {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}),
      SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                        {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}),
      SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}),
      SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE}),
      SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle),
      SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled),
      SettingInfo::Enum(StrId::STR_BIONIC_READING, &CrossPointSettings::bionicReading,
                        {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE}),
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW}),
      SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing),
      SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing),
      SettingInfo::Enum(StrId::STR_TEXT_DARKNESS, &CrossPointSettings::textDarkness,
                        {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK, StrId::STR_EXTRA_DARK}),
      SettingInfo::Enum(StrId::STR_READER_REFRESH_MODE, &CrossPointSettings::readerRefreshMode,
                        {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST, StrId::STR_REFRESH_MODE_HALF,
                         StrId::STR_REFRESH_MODE_FULL}),
      SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS}),
      SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceControlsSettings() {
  static const std::vector<SettingInfo> settings = [] {
    std::vector<SettingInfo> result = {
        SettingInfo::Section(StrId::STR_POWER_BUTTON),
        SettingInfo::EnumMapped(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                                valuesFromArray(POWER_ACTION_LABELS), rawValuesFromArray(POWER_ACTION_VALUES)),
        SettingInfo::EnumMapped(StrId::STR_LONG_PRESS_ACTION, &CrossPointSettings::longPwrBtn,
                                valuesFromArray(POWER_ACTION_LABELS), rawValuesFromArray(POWER_ACTION_VALUES)),
        SettingInfo::Section(StrId::STR_FRONT_BUTTONS),
        SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons),
        SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS_READER, SettingAction::RemapReaderFrontButtons),
        SettingInfo::Enum(StrId::STR_FRONT_BUTTON_HOLD_READER, &CrossPointSettings::longPressButtonBehavior,
                          valuesFromArray(FRONT_LONG_PRESS_LABELS)),
        SettingInfo::Enum(StrId::STR_READING_ORIENTATION_TARGET, &CrossPointSettings::longPressOrientation,
                          valuesFromArray(ORIENTATION_LABELS))
            .visibleWhen(SettingInfo::Visibility::FrontOrientationTarget),
        SettingInfo::EnumMapped(StrId::STR_HOLD_MENU_SHORTCUT, &CrossPointSettings::longPressMenuAction,
                                valuesFromArray(MENU_ACTION_LABELS), rawValuesFromArray(MENU_ACTION_VALUES)),
        SettingInfo::Enum(StrId::STR_FRONT_BTN_ORIENTATION_AWARE, &CrossPointSettings::frontButtonOrientationAware,
                          {StrId::STR_STATE_OFF, StrId::STR_NAV_BUTTONS, StrId::STR_ALL_BUTTONS}),
        SettingInfo::Section(StrId::STR_SIDE_BUTTONS),
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}),
        SettingInfo::Enum(StrId::STR_SIDE_BUTTON_HOLD_READER, &CrossPointSettings::sideButtonLongPress,
                          valuesFromArray(SIDE_LONG_PRESS_LABELS)),
        SettingInfo::Enum(StrId::STR_READING_ORIENTATION_TARGET, &CrossPointSettings::longPressOrientation,
                          valuesFromArray(ORIENTATION_LABELS))
            .visibleWhen(SettingInfo::Visibility::SideOrientationTarget),
        SettingInfo::Enum(StrId::STR_SIDE_BTN_ORIENTATION_AWARE, &CrossPointSettings::sideButtonOrientationAware,
                          {StrId::STR_NO, StrId::STR_YES}),
    };
    if (halTiltSensor.isAvailable()) {
      result.push_back(SettingInfo::Section(StrId::STR_TILT_PAGE_TURN));
      result.push_back(SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                         {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED}));
    }
    return result;
  }();
  return settings;
}

const std::vector<SettingInfo>& getDeviceSystemSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                        {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30}),
      SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles),
      SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network),
      SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync),
      SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                        {StrId::STR_AUTHOR_TITLE, StrId::STR_TITLE_AUTHOR}),
      SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser),
      SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache),
      SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates),
      SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlyControlSettings() {
  static const std::vector<SettingInfo> settings = {};
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlySystemSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network),
      SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync),
      SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser),
      SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache),
      SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates),
      SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlyAppSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Section(StrId::STR_SYNC_DAY),
      SettingInfo::Action(StrId::STR_SYNC_DAY, SettingAction::SyncDay),
      SettingInfo::Action(StrId::STR_TIME_ZONE, SettingAction::TimeZone),
      SettingInfo::Toggle(StrId::STR_DISPLAY_DAY, &CrossPointSettings::displayDay),
      SettingInfo::Enum(StrId::STR_CHOOSE_WIFI, &CrossPointSettings::syncDayWifiChoice,
                        {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_MANUAL}),
      SettingInfo::Enum(StrId::STR_SYNC_DAY_REMINDER_EVERY, &CrossPointSettings::syncDayReminderStarts,
                        {StrId::STR_STATE_OFF, StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30,
                         StrId::STR_NUM_40, StrId::STR_NUM_50, StrId::STR_NUM_60}),
      SettingInfo::Enum(
          StrId::STR_DATE_FORMAT, &CrossPointSettings::dateFormat,
          {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY, StrId::STR_DATE_FORMAT_YYYY_MM_DD}),
      SettingInfo::Section(StrId::STR_READING_STATS),
      SettingInfo::Action(StrId::STR_READING_STATS, SettingAction::ReadingStats),
      SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                        {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60}),
      SettingInfo::Toggle(StrId::STR_SHOW_AFTER_READING, &CrossPointSettings::showStatsAfterReading),
      SettingInfo::Action(StrId::STR_RESET_READING_STATS, SettingAction::ResetReadingStats),
      SettingInfo::Action(StrId::STR_EXPORT_READING_STATS, SettingAction::ExportReadingStats),
      SettingInfo::Action(StrId::STR_IMPORT_READING_STATS, SettingAction::ImportReadingStats),
      SettingInfo::Action(StrId::STR_READING_HEATMAP, SettingAction::ReadingHeatmap),
      SettingInfo::Action(StrId::STR_READING_PROFILE, SettingAction::ReadingProfile),
      SettingInfo::Section(StrId::STR_ACHIEVEMENTS),
      SettingInfo::Action(StrId::STR_ACHIEVEMENTS, SettingAction::Achievements),
      SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled),
      SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups),
      SettingInfo::Action(StrId::STR_RESET_ACHIEVEMENTS, SettingAction::ResetAchievements),
      SettingInfo::Action(StrId::STR_SYNC_WITH_PREV_STATS, SettingAction::SyncAchievementsFromStats),
      SettingInfo::Section(StrId::STR_APPS),
      SettingInfo::Action(StrId::STR_FAVORITES, SettingAction::Favorites),
      SettingInfo::Action(StrId::STR_SLEEP, SettingAction::SleepApp),
      SettingInfo::Action(StrId::STR_IF_FOUND_RETURN_ME, SettingAction::IfFound),
      SettingInfo::Section(StrId::STR_SHORTCUTS_SECTION),
      SettingInfo::Action(StrId::STR_SHORTCUT_LOCATION, SettingAction::ShortcutLocation),
      SettingInfo::Action(StrId::STR_SHORTCUT_VISIBILITY, SettingAction::ShortcutVisibility),
      SettingInfo::Action(StrId::STR_ORDER_HOME_SHORTCUTS, SettingAction::OrderHomeShortcuts),
      SettingInfo::Action(StrId::STR_ORDER_APPS_SHORTCUTS, SettingAction::OrderAppsShortcuts),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlyReaderSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar),
  };
  return settings;
}

std::string getReadingStatsExportPath() { return "/exports/stats_exported"; }

std::string fileNameFromPath(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

size_t utf8CodepointCount(const std::string& text) {
  size_t count = 0;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*ptr != '\0') {
    utf8NextCodepoint(&ptr);
    ++count;
  }
  return count;
}

std::string utf8LimitChars(std::string text, const size_t maxChars) {
  const size_t count = utf8CodepointCount(text);
  if (count <= maxChars) {
    return text;
  }
  utf8TruncateChars(text, count - maxChars);
  return text;
}

std::string getLatestReadingStatsImportPath() {
  const std::string path = getReadingStatsExportPath();
  return Storage.exists(path.c_str()) ? path : std::string();
}

std::string getReadingStatsExportFileName() { return fileNameFromPath(getReadingStatsExportPath()); }

std::string getLatestReadingStatsImportFileName() {
  const std::string path = getLatestReadingStatsImportPath();
  return path.empty() ? std::string() : fileNameFromPath(path);
}

std::string getNetworkSettingValueText() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isApMode = (wifiMode & WIFI_MODE_AP) != 0;
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) != 0 && WiFi.status() == WL_CONNECTED;
  if (isApMode) {
    return "AP";
  }
  if (isStaConnected) {
    const String ssid = WiFi.SSID();
    return ssid.length() > 0 ? std::string(ssid.c_str()) : "WiFi";
  }
  return std::string(tr(STR_STATE_OFF));
}

std::string getReaderFontSettingValueText() {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    return SETTINGS.sdFontFamilyName;
  }
  static constexpr StrId labels[] = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS, StrId::STR_LEXEND};
  const size_t labelCount = sizeof(labels) / sizeof(labels[0]);
  const size_t safeIndex = std::min<size_t>(SETTINGS.fontFamily, labelCount - 1);
  return I18N.get(labels[safeIndex]);
}

std::string getShortcutLocationSettingValueText() {
  int homeCount = 1;  // Apps hub is always in Home.
  int appsCount = 0;
  for (const auto& definition : getShortcutDefinitions()) {
    if (isStatsForkHiddenShortcut(definition)) {
      continue;
    }
    const auto location = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr));
    if (location == CrossPointSettings::SHORTCUT_HOME) {
      ++homeCount;
    } else {
      ++appsCount;
    }
  }
  return "H" + std::to_string(homeCount) + " A" + std::to_string(appsCount);
}

std::string getShortcutVisibilitySettingValueText() {
  int visibleCount = 0;
  int shortcutCount = 0;
  for (const auto& definition : getShortcutDefinitions()) {
    if (isStatsForkHiddenShortcut(definition)) {
      continue;
    }
    ++shortcutCount;
    if (getShortcutVisibility(definition)) {
      ++visibleCount;
    }
  }
  return std::to_string(visibleCount) + "/" + std::to_string(shortcutCount);
}

std::string getShortcutOrderSettingValueText(const ShortcutOrderGroup group) {
  return std::to_string(getShortcutOrderEntries(group).size());
}

size_t getEnumDisplayIndex(const SettingInfo& setting, uint8_t rawValue);
uint8_t getEnumRawValue(const SettingInfo& setting, int displayIndex);

std::string getSettingValueText(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool value = SETTINGS.*(setting.valuePtr);
    return value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (setting.enumValues.empty()) {
      return "";
    }
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    const size_t safeIndex = getEnumDisplayIndex(setting, value);
    return I18N.get(setting.enumValues[safeIndex]);
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  if (setting.type == SettingType::ACTION && setting.action == SettingAction::TimeZone) {
    return TimeUtils::getCurrentTimeZoneLabel();
  }
  if (setting.type == SettingType::ACTION) {
    switch (setting.action) {
      case SettingAction::FontSelection:
        return setting.nameId == StrId::STR_FONT_FAMILY ? getReaderFontSettingValueText() : "";
      case SettingAction::Network:
        return getNetworkSettingValueText();
      case SettingAction::CheckForUpdates:
        return CROSSPOINT_VERSION;
      case SettingAction::Language:
        return I18N.getLanguageName(I18N.getLanguage());
      case SettingAction::ReadingStats: {
        const auto* definition = findShortcutDefinition(ShortcutId::ReadingStats);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::Achievements: {
        const auto* definition = findShortcutDefinition(ShortcutId::Achievements);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::SleepApp: {
        const auto* definition = findShortcutDefinition(ShortcutId::Sleep);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::ShortcutLocation:
        return getShortcutLocationSettingValueText();
      case SettingAction::ShortcutVisibility:
        return getShortcutVisibilitySettingValueText();
      case SettingAction::OrderHomeShortcuts:
        return getShortcutOrderSettingValueText(ShortcutOrderGroup::Home);
      case SettingAction::OrderAppsShortcuts:
        return getShortcutOrderSettingValueText(ShortcutOrderGroup::Apps);
      default:
        break;
    }
  }
  return "";
}

const char* getSettingNameText(const SettingInfo& setting) { return I18N.get(setting.nameId); }

size_t getEnumDisplayIndex(const SettingInfo& setting, const uint8_t rawValue) {
  if (setting.enumRawValues.empty()) {
    return std::min<size_t>(rawValue, setting.enumValues.empty() ? 0 : setting.enumValues.size() - 1);
  }
  const auto it = std::find(setting.enumRawValues.begin(), setting.enumRawValues.end(), rawValue);
  if (it == setting.enumRawValues.end()) {
    return 0;
  }
  return static_cast<size_t>(std::distance(setting.enumRawValues.begin(), it));
}

uint8_t getEnumRawValue(const SettingInfo& setting, const int displayIndex) {
  if (setting.enumRawValues.empty()) {
    return static_cast<uint8_t>(displayIndex);
  }
  if (displayIndex < 0 || displayIndex >= static_cast<int>(setting.enumRawValues.size())) {
    return setting.enumRawValues.front();
  }
  return setting.enumRawValues[static_cast<size_t>(displayIndex)];
}

bool shouldShowDeviceSetting(const SettingInfo& setting) {
  if (setting.visibility == SettingInfo::Visibility::FrontOrientationTarget) {
    return SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_ORIENTATION_CHANGE;
  }
  if (setting.visibility == SettingInfo::Visibility::SideOrientationTarget) {
    return SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_ORIENTATION_CHANGE;
  }
  if (setting.nameId == StrId::STR_FILE_BROWSER_VIEW) {
    return true;
  }
  if (setting.nameId == StrId::STR_SHOW_CURRENT_BOOK_CARD) {
    return SETTINGS.uiTheme == CrossPointSettings::LYRA_VCODEX2;
  }
  return true;
}
}  // namespace

void SettingsActivity::onEnter() {
  Activity::onEnter();

  buildSettingsLists();

  selectedCategoryIndex = std::clamp(initialCategoryIndex, 0, categoryCount - 1);
  selectedSettingIndex = 0;
  enterCategory(selectedCategoryIndex);

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::buildSettingsLists() {
  // Device settings intentionally avoid the shared web/API settings list.
  // That shared list carries dynamic/web metadata and is the wrong dependency
  // for the on-device settings screen.
  const auto& deviceDisplay = getDeviceDisplaySettings();
  const auto& deviceReader = getDeviceReaderSettings();
  const auto& deviceControls = getDeviceControlsSettings();
  const auto& deviceSystem = getDeviceSystemSettings();
  const auto& deviceApps = getDeviceOnlyAppSettings();
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  appSettings.clear();

  displaySettings.reserve(deviceDisplay.size());
  readerSettings.reserve(deviceReader.size());
  controlsSettings.reserve(deviceControls.size());
  systemSettings.reserve(deviceSystem.size());
  appSettings.reserve(deviceApps.size());

  for (const auto& setting : deviceDisplay) {
    if (shouldShowDeviceSetting(setting)) displaySettings.push_back(&setting);
  }
  for (const auto& setting : deviceReader) {
    readerSettings.push_back(&setting);
  }
  for (const auto& setting : deviceControls) {
    controlsSettings.push_back(&setting);
  }
  for (const auto& setting : deviceSystem) {
    if (shouldShowDeviceSetting(setting)) systemSettings.push_back(&setting);
  }
  for (const auto& setting : deviceApps) {
    appSettings.push_back(&setting);
  }
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
  if (currentSettings == nullptr || settingIndex < 0 || settingIndex >= settingsCount) {
    return false;
  }
  const auto& setting = *(*currentSettings)[settingIndex];
  return shouldShowDeviceSetting(setting) && setting.type != SettingType::SECTION;
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

  if (pickerSetting != nullptr) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      closeEnumPicker(false);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      closeEnumPicker(true);
      return;
    }
    buttonNavigator.onNextRelease([this] {
      pickerSelectedIndex =
          ButtonNavigator::nextIndex(pickerSelectedIndex, static_cast<int>(pickerSetting->enumValues.size()));
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      pickerSelectedIndex =
          ButtonNavigator::previousIndex(pickerSelectedIndex, static_cast<int>(pickerSetting->enumValues.size()));
      requestUpdate();
    });
    return;
  }

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
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : firstSelectableSettingIndex();
    enterCategory(selectedCategoryIndex);
  }
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = *(*currentSettings)[selectedSetting];

  if (selectedCategoryIndex == 2 && setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    openEnumPicker(setting);
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const size_t currentDisplayIndex = getEnumDisplayIndex(setting, currentValue);
    const int nextDisplayIndex = ButtonNavigator::nextIndex(static_cast<int>(currentDisplayIndex),
                                                            static_cast<int>(setting.enumValues.size()));
    SETTINGS.*(setting.valuePtr) = getEnumRawValue(setting, nextDisplayIndex);
    if (setting.nameId == StrId::STR_UI_THEME || setting.nameId == StrId::STR_FILE_BROWSER_VIEW) {
      buildSettingsLists();
      enterCategory(selectedCategoryIndex);
      selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
    }
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
      case SettingAction::RemapReaderFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput, true), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::FontSelection:
        startActivityForResult(
            std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                    setting.nameId == StrId::STR_FONT_MANAGER
                                                        ? FontSelectionActivity::Mode::Manage
                                                        : FontSelectionActivity::Mode::Select),
            resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
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
      case SettingAction::SyncDay:
        startActivityForResult(std::make_unique<SyncDayActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::TimeZone:
        startActivityForResult(std::make_unique<TimeZoneSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ReadingStats:
        startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ResetReadingStats:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_READING_STATS_CONFIRM), ""),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                READING_STATS.reset();
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::ExportReadingStats: {
        showTransientPopup(tr(STR_EXPORTING), 20, 120);
        Storage.mkdir("/exports");
        const std::string exportPath = getReadingStatsExportPath();
        if (Storage.exists(exportPath.c_str())) {
          Storage.remove(exportPath.c_str());
        }
        const bool exported = READING_STATS.exportToFile(exportPath);
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
                  if (imported) {
                    ACHIEVEMENTS.rebuildProgressFromCurrentStats();
                  }
                  showTransientPopup(imported ? tr(STR_IMPORT_DONE) : tr(STR_IMPORT_FAILED), imported ? 100 : -1,
                                     imported ? 350 : 700);
                }
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::ReadingHeatmap:
        startActivityForResult(std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ReadingProfile:
        startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Achievements:
        startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput), resultHandler);
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
      case SettingAction::Bookmarks:
        startActivityForResult(std::make_unique<BookmarksAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Favorites:
        startActivityForResult(std::make_unique<FavoritesAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SleepApp:
        startActivityForResult(std::make_unique<SleepAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::IfFound:
        startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else if (setting.type == SettingType::SECTION) {
    return;
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

  if (setting.valuePtr == &CrossPointSettings::textDarkness) {
    renderer.setTextDarkness(SETTINGS.textDarkness);
    renderer.requestNextFullRefresh();
  }

  SETTINGS.saveToFile();
}

void SettingsActivity::openEnumPicker(const SettingInfo& setting) {
  pickerSetting = &setting;
  pickerSelectedIndex = setting.valuePtr != nullptr ? static_cast<int>(getEnumDisplayIndex(setting, SETTINGS.*(setting.valuePtr))) : 0;
  if (pickerSelectedIndex < 0 || pickerSelectedIndex >= static_cast<int>(setting.enumValues.size())) {
    pickerSelectedIndex = 0;
  }
  requestUpdate();
}

void SettingsActivity::closeEnumPicker(const bool apply) {
  if (pickerSetting == nullptr) {
    return;
  }

  if (apply && pickerSetting->valuePtr != nullptr) {
    SETTINGS.*(pickerSetting->valuePtr) = getEnumRawValue(*pickerSetting, pickerSelectedIndex);
    SETTINGS.saveToFile();
  }

  pickerSetting = nullptr;
  requestUpdate();
}

void SettingsActivity::renderEnumPicker() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 getSettingNameText(*pickerSetting));
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(pickerSetting->enumValues.size()),
      pickerSelectedIndex,
      [this](int index) { return std::string(I18N.get(pickerSetting->enumValues[static_cast<size_t>(index)])); }, nullptr,
      nullptr, nullptr, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SettingsActivity::renderAppSettingsList(const Rect& rect) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& settings = *currentSettings;
  if (settings.empty() || rect.height <= 0) {
    return;
  }

  const bool controlsCategory = selectedCategoryIndex == 2;
  const int rowHeight = controlsCategory ? 48 : metrics.listRowHeight;
  const int sectionHeight = controlsCategory ? 30 : 42;
  const int sidePadding = metrics.contentSidePadding;
  constexpr int scrollBarWidth = 4;
  constexpr int scrollBarGap = 6;
  const int rowX = rect.x + sidePadding;
  const int rowWidth = rect.width - sidePadding * 2 - scrollBarWidth - scrollBarGap;
  const int viewportHeight = rect.height;

  auto getItemHeight = [rowHeight, sectionHeight](const SettingInfo* setting) {
    if (!shouldShowDeviceSetting(*setting)) {
      return 0;
    }
    return setting->type == SettingType::SECTION ? sectionHeight : rowHeight;
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

    if (firstVisibleIndex > 0 && settings[firstVisibleIndex - 1]->type == SettingType::SECTION) {
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
    if (itemHeight == 0) {
      continue;
    }
    if (renderedHeight + itemHeight > viewportHeight) {
      break;
    }

    if (setting->type == SettingType::SECTION) {
      const int sectionTextY = currentY + (controlsCategory ? 5 : 4);
      const int sectionLineY = currentY + itemHeight - (controlsCategory ? 4 : 5);
      renderer.drawText(UI_10_FONT_ID, rowX, sectionTextY, getSettingNameText(*setting), true, EpdFontFamily::BOLD);
      renderer.drawLine(rowX, sectionLineY, rowX + rowWidth, sectionLineY,
                        SETTINGS.uiTheme == CrossPointSettings::LYRA_VCODEX2 ? 1 : 2, true);
      currentY += itemHeight;
      renderedHeight += itemHeight;
      continue;
    }

    const bool selected = selectedSettingIndex == index + 1;
    const int rowTopInset = controlsCategory ? 2 : 3;
    const int rowBottomInset = controlsCategory ? 4 : 5;
    const Rect rowRect{rowX, currentY + rowTopInset, rowWidth, itemHeight - rowTopInset - rowBottomInset};
    const Rect highlightRect{rowRect.x, rowRect.y, rowRect.width, rowRect.height};
    if (selected) {
      if (SETTINGS.uiTheme == CrossPointSettings::LYRA_VCODEX2) {
        renderer.drawRoundedRect(highlightRect.x, highlightRect.y, highlightRect.width, highlightRect.height, 1, 6,
                                 true);
        if (!controlsCategory) {
          renderer.drawRoundedRect(highlightRect.x + 2, highlightRect.y + 2, highlightRect.width - 4,
                                   highlightRect.height - 4, 1, 5, true);
        }
      } else {
        renderer.fillRectDither(highlightRect.x, highlightRect.y, highlightRect.width, highlightRect.height,
                                Color::LightGray);
        renderer.drawRect(highlightRect.x, highlightRect.y, highlightRect.width, highlightRect.height);
      }
    }

    const std::string valueText = getSettingValueText(*setting);
    const bool showExportFileName =
        setting->type == SettingType::ACTION && setting->action == SettingAction::ExportReadingStats;
    const bool showImportFileName =
        setting->type == SettingType::ACTION && setting->action == SettingAction::ImportReadingStats;
    const std::string sideNote = showExportFileName
                                     ? getReadingStatsExportFileName()
                                     : (showImportFileName ? getLatestReadingStatsImportFileName() : std::string());
    const int leftPadding = controlsCategory ? 14 : 12;
    const int rightPadding = controlsCategory ? 14 : 12;
    if (controlsCategory) {
      const int valueMaxWidth = valueText.empty() ? 0 : std::min(rowRect.width / 2, 170);
      const std::string valueLine =
          valueText.empty()
              ? std::string()
              : renderer.truncatedText(SMALL_FONT_ID, valueText.c_str(), valueMaxWidth, EpdFontFamily::BOLD);
      const int valueWidth =
          valueLine.empty() ? 0 : renderer.getTextWidth(SMALL_FONT_ID, valueLine.c_str(), EpdFontFamily::BOLD);
      const int labelWidth =
          rowRect.width - leftPadding - rightPadding - (valueWidth > 0 ? valueWidth + 16 : 0);
      const std::string titleText =
          renderer.truncatedText(UI_10_FONT_ID, getSettingNameText(*setting), labelWidth, EpdFontFamily::REGULAR);

      const int labelY = rowRect.y + std::max(5, (rowRect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
      const int valueY = rowRect.y + std::max(5, (rowRect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2);
      renderer.drawText(UI_10_FONT_ID, rowRect.x + leftPadding, labelY, titleText.c_str(), true,
                        EpdFontFamily::REGULAR);
      if (!valueLine.empty()) {
        renderer.drawText(SMALL_FONT_ID, rowRect.x + rowRect.width - rightPadding - valueWidth, valueY,
                          valueLine.c_str(), true, EpdFontFamily::BOLD);
      }
    } else if (showExportFileName || showImportFileName) {
      const int sideNoteMaxWidth = rowRect.width / 2 - leftPadding - rightPadding;
      const std::string truncatedSideNote =
          sideNote.empty()
              ? std::string()
              : renderer.truncatedText(SMALL_FONT_ID, sideNote.c_str(), sideNoteMaxWidth, EpdFontFamily::REGULAR);
      const int sideNoteWidth =
          truncatedSideNote.empty()
              ? 0
              : renderer.getTextWidth(SMALL_FONT_ID, truncatedSideNote.c_str(), EpdFontFamily::REGULAR);

      const int labelWidth = rowRect.width - leftPadding - rightPadding - (sideNoteWidth > 0 ? sideNoteWidth + 12 : 0);
      const std::string titleText =
          renderer.truncatedText(UI_10_FONT_ID, getSettingNameText(*setting), labelWidth, EpdFontFamily::REGULAR);
      const int textY = rowRect.y + std::max(6, (rowRect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
      renderer.drawText(UI_10_FONT_ID, rowRect.x + leftPadding, textY, titleText.c_str(), true,
                        EpdFontFamily::REGULAR);
      if (!truncatedSideNote.empty()) {
        renderer.drawText(SMALL_FONT_ID, rowRect.x + rowRect.width - rightPadding - sideNoteWidth, textY + 2,
                          truncatedSideNote.c_str(), true, EpdFontFamily::REGULAR);
      }
    } else {
      const int valueWidth =
          valueText.empty() ? 0 : renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str(), EpdFontFamily::REGULAR);
      const int labelWidth = rowRect.width - leftPadding - rightPadding - (valueWidth > 0 ? valueWidth + 12 : 0);
      const std::string titleText =
          renderer.truncatedText(UI_10_FONT_ID, getSettingNameText(*setting), labelWidth, EpdFontFamily::REGULAR);

      const int textY = rowRect.y + std::max(6, (rowRect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
      renderer.drawText(UI_10_FONT_ID, rowRect.x + leftPadding, textY, titleText.c_str(), true,
                        EpdFontFamily::REGULAR);
      if (!valueText.empty()) {
        renderer.drawText(UI_10_FONT_ID, rowRect.x + rowRect.width - rightPadding - valueWidth, textY, valueText.c_str(),
                          true, EpdFontFamily::REGULAR);
      }
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
  if (pickerSetting != nullptr) {
    renderEnumPicker();
    return;
  }

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* settingsTitle = tr(STR_SETTINGS_TITLE);
  const char* selectedCategoryLabel = I18N.get(categoryNames[selectedCategoryIndex]);
  const char* firmwareVersion = CROSSPOINT_VERSION;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, settingsTitle, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  const int titleX = metrics.contentSidePadding;
  const int titleY = metrics.topPadding + metrics.batteryBarHeight + 3;
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, settingsTitle, EpdFontFamily::BOLD);
  const int categoryGap = 10;
  const int categoryX = titleX + titleWidth + categoryGap;
  const int versionWidth = renderer.getTextWidth(SMALL_FONT_ID, firmwareVersion, EpdFontFamily::REGULAR);
  const int versionX = pageWidth - metrics.contentSidePadding - versionWidth;
  const int versionGap = 12;
  const int categoryMaxWidth = std::max(0, versionX - categoryX - versionGap);
  if (categoryMaxWidth > 24) {
    const std::string headerCategory =
        renderer.truncatedText(SMALL_FONT_ID, selectedCategoryLabel, categoryMaxWidth, EpdFontFamily::REGULAR);
    if (!headerCategory.empty()) {
      const std::string categoryPrefix = "/ ";
      renderer.drawText(SMALL_FONT_ID, categoryX, titleY + 4, categoryPrefix.c_str(), true, EpdFontFamily::REGULAR);
      renderer.drawText(
          SMALL_FONT_ID,
          categoryX + renderer.getTextWidth(SMALL_FONT_ID, categoryPrefix.c_str(), EpdFontFamily::REGULAR), titleY + 4,
          headerCategory.c_str(), true, EpdFontFamily::REGULAR);
    }
  }
  renderer.drawText(SMALL_FONT_ID, versionX, titleY + 4, firmwareVersion, true, EpdFontFamily::REGULAR);

  std::vector<std::string> tabLabels;
  tabLabels.reserve(categoryCount);
  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    const char* fullLabel = I18N.get(categoryNames[i]);
    tabLabels.push_back(
        utf8LimitChars(fullLabel != nullptr ? std::string(fullLabel) : std::string(), SETTINGS_TAB_MAX_CHARS));
    const bool compact =
        utf8CodepointCount(fullLabel != nullptr ? std::string(fullLabel) : std::string()) > SETTINGS_TAB_MAX_CHARS;
    tabs.push_back({tabLabels.back().c_str(), selectedCategoryIndex == i, compact});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  constexpr int listBottomGap = 10;
  const Rect listRect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing,
                      pageWidth,
                      pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                                    metrics.buttonHintsHeight + metrics.verticalSpacing * 2 + listBottomGap)};
  const auto& settings = *currentSettings;
  if (selectedCategoryIndex == 2 || selectedCategoryIndex == 4) {
    renderAppSettingsList(listRect);
  } else {
    GUI.drawList(
        renderer, listRect, settingsCount, selectedSettingIndex - 1,
        [&settings](int index) { return std::string(getSettingNameText(*settings[index])); }, nullptr, nullptr,
        [&settings](int i) { return getSettingValueText(*settings[i]); }, true);
  }

  // Draw help text
  const char* confirmLabel = nullptr;
  if (selectedSettingIndex == 0) {
    confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
  } else {
    const auto& selectedSetting = *(*currentSettings)[selectedSettingIndex - 1];
    confirmLabel = selectedCategoryIndex == 2 && selectedSetting.type == SettingType::ENUM
                       ? tr(STR_OPEN)
                       : ((selectedSetting.type == SettingType::ACTION || selectedSetting.type == SettingType::SECTION)
                              ? tr(STR_SELECT)
                              : tr(STR_TOGGLE));
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
