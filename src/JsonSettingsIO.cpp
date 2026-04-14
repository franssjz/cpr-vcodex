#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Stream.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "AchievementsStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"
#include "activities/settings/SettingsActivity.h"
#include "util/BookIdentity.h"
#include "util/ShortcutRegistry.h"
#include "util/TimeZoneRegistry.h"

namespace {
constexpr uint8_t FONT_SIZE_SCHEMA_VERSION = 2;
constexpr uint8_t FONT_FAMILY_SCHEMA_VERSION = 2;

class HalFileStream : public Stream {
 public:
  explicit HalFileStream(HalFile& file) : file(file) {}

  int available() override { return file.available() + (peekedByte >= 0 ? 1 : 0); }

  int read() override {
    if (peekedByte >= 0) {
      const int byte = peekedByte;
      peekedByte = -1;
      return byte;
    }
    return file.read();
  }

  int peek() override {
    if (peekedByte < 0) {
      peekedByte = file.read();
    }
    return peekedByte;
  }

  void flush() override { file.flush(); }

  size_t write(uint8_t value) override { return file.write(value); }

 private:
  HalFile& file;
  int peekedByte = -1;
};

bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForWrite(moduleName, path, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for write: %s", path);
    return false;
  }

  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  return written > 0;
}

bool loadJsonDocumentFromFile(const char* moduleName, const char* path, JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for read: %s", path);
    return false;
  }

  HalFileStream stream(file);
  auto error = deserializeJson(doc, stream);
  file.close();
  if (error) {
    LOG_ERR(moduleName, "JSON parse error: %s", error.c_str());
    return false;
  }
  return true;
}

// Keep settings persistence independent from the large shared web/device settings list.
// Constructing that shared list pulls in many std::function-backed entries that the device
// does not need just to load/save /.crosspoint/settings.json.
const std::vector<SettingInfo>& getPersistedSettingsList() {
  static const std::vector<SettingInfo> list = {
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                         StrId::STR_COVER_CUSTOM},
                        "sleepScreen", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                        {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                        "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                        StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(
          StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
          "refreshFrequency", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                        {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                         StrId::STR_THEME_LYRA_CUSTOM},
                        "uiTheme", StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                          StrId::STR_CAT_DISPLAY),

      SettingInfo::Toggle(StrId::STR_DISPLAY_DAY, &CrossPointSettings::displayDay, "displayDay", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_SYNC_DAY_REMINDER_EVERY, &CrossPointSettings::syncDayReminderStarts,
                        {StrId::STR_STATE_OFF, StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30,
                         StrId::STR_NUM_40, StrId::STR_NUM_50, StrId::STR_NUM_60},
                        "syncDayReminderStarts", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_DATE_FORMAT, &CrossPointSettings::dateFormat,
                        {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY,
                         StrId::STR_DATE_FORMAT_YYYY_MM_DD},
                        "dateFormat", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                        {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60},
                        "dailyGoalTarget", StrId::STR_APPS),
      SettingInfo::Toggle(StrId::STR_SHOW_AFTER_READING, &CrossPointSettings::showStatsAfterReading,
                          "showStatsAfterReading", StrId::STR_APPS),
      SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled,
                          "achievementsEnabled", StrId::STR_APPS),
      SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups,
                          "achievementPopups", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_BROWSE_FILES, &CrossPointSettings::browseFilesShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "browseFilesShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_STATS_SHORTCUT, &CrossPointSettings::statsShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "statsShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_SYNC_DAY, &CrossPointSettings::syncDayShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "syncDayShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_SETTINGS_TITLE, &CrossPointSettings::settingsShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "settingsShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_READING_STATS, &CrossPointSettings::readingStatsShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readingStatsShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_READING_HEATMAP, &CrossPointSettings::readingHeatmapShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readingHeatmapShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_READING_PROFILE, &CrossPointSettings::readingProfileShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readingProfileShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_ACHIEVEMENTS, &CrossPointSettings::achievementsShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "achievementsShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_IF_FOUND_RETURN_ME, &CrossPointSettings::ifFoundShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "ifFoundShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_README, &CrossPointSettings::readMeShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readMeShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_MENU_RECENT_BOOKS, &CrossPointSettings::recentBooksShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "recentBooksShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_BOOKMARKS, &CrossPointSettings::bookmarksShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "bookmarksShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_FILE_TRANSFER, &CrossPointSettings::fileTransferShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "fileTransferShortcut", StrId::STR_APPS),
      SettingInfo::Enum(StrId::STR_SLEEP, &CrossPointSettings::sleepShortcut,
                        {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "sleepShortcut", StrId::STR_APPS),

      SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS, StrId::STR_LEXEND}, "fontFamily",
                        StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                        {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                         StrId::STR_X_LARGE},
                        "fontSize", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                        {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing", StrId::STR_CAT_READER),
      SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin",
                         StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE},
                        "paragraphAlignment", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                          StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                          StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                        "orientation", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                          "extraParagraphSpacing", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                          StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_TEXT_DARKNESS, &CrossPointSettings::textDarkness,
                        {StrId::STR_NORMAL, StrId::STR_DARK, StrId::STR_EXTRA_DARK}, "textDarkness",
                        StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                        "imageRendering", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_READER_REFRESH_MODE, &CrossPointSettings::readerRefreshMode,
                        {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST, StrId::STR_REFRESH_MODE_HALF,
                         StrId::STR_REFRESH_MODE_FULL},
                        "readerRefreshMode", StrId::STR_CAT_READER),

      SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                        {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}, "sideButtonLayout", StrId::STR_CAT_CONTROLS),
      SettingInfo::Toggle(StrId::STR_LONG_PRESS_SKIP, &CrossPointSettings::longPressChapterSkip,
                          "longPressChapterSkip", StrId::STR_CAT_CONTROLS),
      SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                        {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN}, "shortPwrBtn",
                        StrId::STR_CAT_CONTROLS),

      SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                        {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30},
                        "sleepTimeout", StrId::STR_CAT_SYSTEM),
      SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                          StrId::STR_CAT_SYSTEM),

      SettingInfo::String(StrId::STR_OPDS_SERVER_URL, SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl),
                          "opdsServerUrl", StrId::STR_OPDS_BROWSER),
      SettingInfo::String(StrId::STR_USERNAME, SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername), "opdsUsername",
                          StrId::STR_OPDS_BROWSER),
      SettingInfo::String(StrId::STR_PASSWORD, SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword), "opdsPassword",
                          StrId::STR_OPDS_BROWSER)
          .withObfuscated(),

      SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                          "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                          "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                        StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                        {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                        "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                        StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
  };
  return list;
}
}  // namespace

// Convert legacy settings.
void applyLegacyStatusBarSettings(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
    case CrossPointSettings::NONE:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::NO_PROGRESS:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::ONLY_BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::CHAPTER_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::CHAPTER_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::FULL:
    default:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
  }
}

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  doc["lastSleepImage"] = s.lastSleepImage;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  doc["lastKnownValidTimestamp"] = s.lastKnownValidTimestamp;
  doc["syncDayReminderStartCount"] = s.syncDayReminderStartCount;
  doc["syncDayReminderLatched"] = s.syncDayReminderLatched;

  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadState(CrossPointState& s, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  s.openEpubPath = doc["openEpubPath"] | std::string("");
  s.lastSleepImage = doc["lastSleepImage"] | (uint8_t)UINT8_MAX;
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.lastKnownValidTimestamp = doc["lastKnownValidTimestamp"] | static_cast<uint32_t>(0);
  s.syncDayReminderStartCount = doc["syncDayReminderStartCount"] | (uint8_t)0;
  s.syncDayReminderLatched = doc["syncDayReminderLatched"] | false;
  return true;
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;

  for (const auto& info : getPersistedSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        doc[std::string(info.key) + "_obf"] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["fontFamilySchemaVersion"] = FONT_FAMILY_SCHEMA_VERSION;
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["timeZonePreset"] = TimeZoneRegistry::clampPresetIndex(s.timeZonePreset);
  doc["sleepDirectory"] = s.sleepDirectory;
  doc["sleepImageOrder"] = s.sleepImageOrder;
  doc["appsHubShortcutOrder"] = s.appsHubShortcutOrder;
  doc["browseFilesShortcutOrder"] = s.browseFilesShortcutOrder;
  doc["statsShortcutOrder"] = s.statsShortcutOrder;
  doc["syncDayShortcutOrder"] = s.syncDayShortcutOrder;
  doc["settingsShortcutOrder"] = s.settingsShortcutOrder;
  doc["readingStatsShortcutOrder"] = s.readingStatsShortcutOrder;
  doc["readingHeatmapShortcutOrder"] = s.readingHeatmapShortcutOrder;
  doc["readingProfileShortcutOrder"] = s.readingProfileShortcutOrder;
  doc["achievementsShortcutOrder"] = s.achievementsShortcutOrder;
  doc["ifFoundShortcutOrder"] = s.ifFoundShortcutOrder;
  doc["readMeShortcutOrder"] = s.readMeShortcutOrder;
  doc["recentBooksShortcutOrder"] = s.recentBooksShortcutOrder;
  doc["bookmarksShortcutOrder"] = s.bookmarksShortcutOrder;
  doc["fileTransferShortcutOrder"] = s.fileTransferShortcutOrder;
  doc["sleepShortcutOrder"] = s.sleepShortcutOrder;
  doc["browseFilesShortcutVisible"] = s.browseFilesShortcutVisible;
  doc["statsShortcutVisible"] = s.statsShortcutVisible;
  doc["syncDayShortcutVisible"] = s.syncDayShortcutVisible;
  doc["settingsShortcutVisible"] = 1;
  doc["readingStatsShortcutVisible"] = s.readingStatsShortcutVisible;
  doc["readingHeatmapShortcutVisible"] = s.readingHeatmapShortcutVisible;
  doc["readingProfileShortcutVisible"] = s.readingProfileShortcutVisible;
  doc["achievementsShortcutVisible"] = s.achievementsShortcutVisible;
  doc["ifFoundShortcutVisible"] = s.ifFoundShortcutVisible;
  doc["readMeShortcutVisible"] = s.readMeShortcutVisible;
  doc["recentBooksShortcutVisible"] = s.recentBooksShortcutVisible;
  doc["bookmarksShortcutVisible"] = s.bookmarksShortcutVisible;
  doc["fileTransferShortcutVisible"] = s.fileTransferShortcutVisible;
  doc["sleepShortcutVisible"] = s.sleepShortcutVisible;
  doc["fontSizeSchemaVersion"] = FONT_SIZE_SCHEMA_VERSION;

  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  // Legacy migration: if statusBarChapterPageCount is absent this is a pre-refactor settings file.
  // Populate s with migrated values now so the generic loop below picks them up as defaults and clamps them.
  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  for (const auto& info : getPersistedSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      const std::string fieldDefault = strPtr;  // current buffer = struct-initializer default
      std::string val;
      if (info.obfuscated) {
        bool ok = false;
        val = obfuscation::deobfuscateFromBase64(doc[std::string(info.key) + "_obf"] | "", &ok);
        if (!ok || val.empty()) {
          val = doc[info.key] | fieldDefault;
          if (val != fieldDefault && needsResave) *needsResave = true;
        }
      } else {
        val = doc[info.key] | fieldDefault;
      }
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        if (needsResave) *needsResave = true;
        continue;
      }
      strncpy(destPtr, val.c_str(), info.stringMaxLen - 1);
      destPtr[info.stringMaxLen - 1] = '\0';
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  const uint8_t fontSizeSchemaVersion = doc["fontSizeSchemaVersion"] | static_cast<uint8_t>(0);
  if (fontSizeSchemaVersion < FONT_SIZE_SCHEMA_VERSION && !doc["fontSize"].isNull()) {
    const uint8_t legacyFontSize = doc["fontSize"] | static_cast<uint8_t>(CrossPointSettings::MEDIUM - 1);
    if (legacyFontSize < static_cast<uint8_t>(CrossPointSettings::EXTRA_LARGE)) {
      s.fontSize = static_cast<uint8_t>(legacyFontSize + 1);
      if (needsResave) *needsResave = true;
    }
  }
  const uint8_t fontFamilySchemaVersion = doc["fontFamilySchemaVersion"] | FONT_FAMILY_SCHEMA_VERSION;
  const uint8_t rawFontFamily = doc["fontFamily"] | s.fontFamily;
  if (fontFamilySchemaVersion < FONT_FAMILY_SCHEMA_VERSION && rawFontFamily == 3) {
    s.fontFamily = CrossPointSettings::LEXEND;
    if (needsResave) *needsResave = true;
  } else if (rawFontFamily >= static_cast<uint8_t>(CrossPointSettings::FONT_FAMILY_COUNT)) {
    s.fontFamily = CrossPointSettings::BOOKERLY;
    if (needsResave) *needsResave = true;
  } else {
    s.fontFamily = rawFontFamily;
  }

  using S = CrossPointSettings;
  s.frontButtonBack =
      clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM, S::FRONT_BUTTON_HARDWARE_COUNT,
                               S::FRONT_HW_CONFIRM);
  s.frontButtonLeft =
      clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  CrossPointSettings::validateFrontButtonMapping(s);
  s.timeZonePreset =
      TimeZoneRegistry::clampPresetIndex(doc["timeZonePreset"] | TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);

  {
    const std::string sleepDirectory = doc["sleepDirectory"] | std::string("");
    strncpy(s.sleepDirectory, sleepDirectory.c_str(), sizeof(s.sleepDirectory) - 1);
    s.sleepDirectory[sizeof(s.sleepDirectory) - 1] = '\0';
  }
  s.sleepImageOrder = clamp(doc["sleepImageOrder"] | (uint8_t)CrossPointSettings::SLEEP_IMAGE_SHUFFLE,
                            CrossPointSettings::SLEEP_IMAGE_ORDER_COUNT,
                            CrossPointSettings::SLEEP_IMAGE_SHUFFLE);

  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.appsHubShortcutOrder = clamp(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount,
                                 s.appsHubShortcutOrder);
  s.browseFilesShortcutOrder = clamp(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount,
                                     s.browseFilesShortcutOrder);
  s.statsShortcutOrder =
      clamp(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcutOrder =
      clamp(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcutOrder =
      clamp(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcutOrder = clamp(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder,
                                      shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcutOrder = clamp(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder,
                                        shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcutOrder = clamp(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder,
                                        shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcutOrder = clamp(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder,
                                      shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcutOrder =
      clamp(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcutOrder =
      clamp(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcutOrder = clamp(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount,
                                     s.recentBooksShortcutOrder);
  s.bookmarksShortcutOrder =
      clamp(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.fileTransferShortcutOrder = clamp(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder,
                                      shortcutOrderCount, s.fileTransferShortcutOrder);
  s.sleepShortcutOrder =
      clamp(doc["sleepShortcutOrder"] | s.sleepShortcutOrder, shortcutOrderCount, s.sleepShortcutOrder);
  s.browseFilesShortcutVisible =
      clamp(doc["browseFilesShortcutVisible"] | s.browseFilesShortcutVisible, static_cast<uint8_t>(2),
            s.browseFilesShortcutVisible);
  s.statsShortcutVisible =
      clamp(doc["statsShortcutVisible"] | s.statsShortcutVisible, static_cast<uint8_t>(2), s.statsShortcutVisible);
  s.syncDayShortcutVisible =
      clamp(doc["syncDayShortcutVisible"] | s.syncDayShortcutVisible, static_cast<uint8_t>(2), s.syncDayShortcutVisible);
  s.settingsShortcutVisible =
      clamp(doc["settingsShortcutVisible"] | s.settingsShortcutVisible, static_cast<uint8_t>(2),
            s.settingsShortcutVisible);
  if (s.settingsShortcutVisible == 0) {
    s.settingsShortcutVisible = 1;
    if (needsResave) *needsResave = true;
  }
  s.readingStatsShortcutVisible =
      clamp(doc["readingStatsShortcutVisible"] | s.readingStatsShortcutVisible, static_cast<uint8_t>(2),
            s.readingStatsShortcutVisible);
  s.readingHeatmapShortcutVisible =
      clamp(doc["readingHeatmapShortcutVisible"] | s.readingHeatmapShortcutVisible, static_cast<uint8_t>(2),
            s.readingHeatmapShortcutVisible);
  s.readingProfileShortcutVisible =
      clamp(doc["readingProfileShortcutVisible"] | s.readingProfileShortcutVisible, static_cast<uint8_t>(2),
            s.readingProfileShortcutVisible);
  s.achievementsShortcutVisible =
      clamp(doc["achievementsShortcutVisible"] | s.achievementsShortcutVisible, static_cast<uint8_t>(2),
            s.achievementsShortcutVisible);
  s.ifFoundShortcutVisible =
      clamp(doc["ifFoundShortcutVisible"] | s.ifFoundShortcutVisible, static_cast<uint8_t>(2),
            s.ifFoundShortcutVisible);
  s.readMeShortcutVisible =
      clamp(doc["readMeShortcutVisible"] | s.readMeShortcutVisible, static_cast<uint8_t>(2), s.readMeShortcutVisible);
  s.recentBooksShortcutVisible =
      clamp(doc["recentBooksShortcutVisible"] | s.recentBooksShortcutVisible, static_cast<uint8_t>(2),
            s.recentBooksShortcutVisible);
  s.bookmarksShortcutVisible =
      clamp(doc["bookmarksShortcutVisible"] | s.bookmarksShortcutVisible, static_cast<uint8_t>(2),
            s.bookmarksShortcutVisible);
  s.fileTransferShortcutVisible =
      clamp(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible, static_cast<uint8_t>(2),
            s.fileTransferShortcutVisible);
  s.sleepShortcutVisible =
      clamp(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, static_cast<uint8_t>(2), s.sleepShortcutVisible);

  normalizeShortcutOrderSettings(s);

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

// ---- KOReaderCredentialStore ----

bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["username"] = store.getUsername();
  doc["password_obf"] = obfuscation::obfuscateToBase64(store.getPassword());
  doc["serverUrl"] = store.getServerUrl();
  doc["matchMethod"] = static_cast<uint8_t>(store.getMatchMethod());

  return saveJsonDocumentToFile("KRS", path, doc);
}

bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.username = doc["username"] | std::string("");
  bool ok = false;
  store.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok || store.password.empty()) {
    store.password = doc["password"] | std::string("");
    if (!store.password.empty() && needsResave) *needsResave = true;
  }
  store.serverUrl = doc["serverUrl"] | std::string("");
  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  store.matchMethod = static_cast<DocumentMatchMethod>(method);

  LOG_DBG("KRS", "Loaded KOReader credentials for user: %s", store.username.c_str());
  return true;
}

// ---- WifiCredentialStore ----

bool JsonSettingsIO::saveWifi(const WifiCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["lastConnectedSsid"] = store.getLastConnectedSsid();

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto& cred : store.getCredentials()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
  }

  return saveJsonDocumentToFile("WCS", path, doc);
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WCS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.lastConnectedSsid = doc["lastConnectedSsid"] | std::string("");

  store.credentials.clear();
  JsonArray arr = doc["credentials"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.credentials.size() >= store.MAX_NETWORKS) break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");
    bool ok = false;
    cred.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || cred.password.empty()) {
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty() && needsResave) *needsResave = true;
    }
    store.credentials.push_back(cred);
  }

  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", store.credentials.size());
  return true;
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  return saveJsonDocumentToFile("RBS", path, doc);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RBS", "JSON parse error: %s", error.c_str());
    return false;
  }

  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);
  store.recentBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    if (formatVersion < 2) {
      book.bookId.clear();
    }
    store.recentBooks.push_back(book);
  }

  store.normalizeBooks();
  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

bool JsonSettingsIO::loadRecentBooksFromFile(RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  if (!loadJsonDocumentFromFile("RBS", path, doc)) {
    return false;
  }

  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);
  store.recentBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    if (formatVersion < 2) {
      book.bookId.clear();
    }
    store.recentBooks.push_back(book);
  }

  store.normalizeBooks();
  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- ReadingStatsStore ----

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 4;

  JsonArray days = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.getReadingDays()) {
    JsonObject dayObj = days.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray legacyDays = doc["legacyReadingDays"].to<JsonArray>();
  for (const auto& day : store.legacyReadingDays) {
    JsonObject dayObj = legacyDays.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray sessionLog = doc["sessionLog"].to<JsonArray>();
  for (const auto& session : store.getSessionLog()) {
    JsonObject sessionObj = sessionLog.add<JsonObject>();
    sessionObj["dayOrdinal"] = session.dayOrdinal;
    sessionObj["sessionMs"] = session.sessionMs;
  }

  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = books.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    JsonArray knownPaths = obj["knownPaths"].to<JsonArray>();
    for (const auto& knownPath : book.knownPaths) {
      knownPaths.add(knownPath);
    }
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["chapterTitle"] = book.chapterTitle;
    obj["totalReadingMs"] = book.totalReadingMs;
    obj["sessions"] = book.sessions;
    obj["lastSessionMs"] = book.lastSessionMs;
    obj["firstReadAt"] = book.firstReadAt;
    obj["lastReadAt"] = book.lastReadAt;
    obj["lastProgressPercent"] = book.lastProgressPercent;
    obj["chapterProgressPercent"] = book.chapterProgressPercent;
    obj["completed"] = book.completed;

    JsonArray bookDays = obj["readingDays"].to<JsonArray>();
    for (const auto& day : book.readingDays) {
      JsonObject dayObj = bookDays.add<JsonObject>();
      dayObj["dayOrdinal"] = day.dayOrdinal;
      dayObj["readingMs"] = day.readingMs;
    }
  }

  return saveJsonDocumentToFile("RST", path, doc);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RST", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = false;

  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  auto appendReadingDays = [](std::vector<ReadingDayStats>& destination, JsonArray source) {
    for (JsonVariant value : source) {
      ReadingDayStats day;
      if (value.is<JsonObject>()) {
        JsonObject obj = value.as<JsonObject>();
        day.dayOrdinal = obj["dayOrdinal"] | static_cast<uint32_t>(0);
        day.readingMs = obj["readingMs"] | static_cast<uint64_t>(0);
      } else {
        day.dayOrdinal = value | static_cast<uint32_t>(0);
        day.readingMs = 0;
      }
      if (day.dayOrdinal != 0) {
        destination.push_back(day);
      }
    }
  };

  appendReadingDays(store.readingDays, doc["readingDays"].as<JsonArray>());
  if (formatVersion >= 2) {
    appendReadingDays(store.legacyReadingDays, doc["legacyReadingDays"].as<JsonArray>());
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    for (JsonObject sessionObj : doc["sessionLog"].as<JsonArray>()) {
      ReadingSessionLogEntry session;
      session.dayOrdinal = sessionObj["dayOrdinal"] | static_cast<uint32_t>(0);
      session.sessionMs = sessionObj["sessionMs"] | static_cast<uint32_t>(0);
      if (session.dayOrdinal != 0 && session.sessionMs != 0) {
        store.sessionLog.push_back(session);
      }
    }
  } else {
    store.dirty = true;
  }

  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject obj : books) {
    ReadingBookStats book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
    }
    for (JsonVariant value : obj["knownPaths"].as<JsonArray>()) {
      const std::string knownPath = value | std::string("");
      if (!knownPath.empty()) {
        book.knownPaths.push_back(knownPath);
      }
    }
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.chapterTitle = obj["chapterTitle"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | static_cast<uint64_t>(0);
    book.sessions = obj["sessions"] | static_cast<uint32_t>(0);
    book.lastSessionMs = obj["lastSessionMs"] | static_cast<uint32_t>(0);
    book.firstReadAt = obj["firstReadAt"] | static_cast<uint32_t>(0);
    book.lastReadAt = obj["lastReadAt"] | static_cast<uint32_t>(0);
    book.lastProgressPercent = obj["lastProgressPercent"] | static_cast<uint8_t>(0);
    book.chapterProgressPercent = obj["chapterProgressPercent"] | static_cast<uint8_t>(0);
    book.completed = obj["completed"] | false;
    if (formatVersion >= 2) {
      appendReadingDays(book.readingDays, obj["readingDays"].as<JsonArray>());
    }
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
    store.books.push_back(std::move(book));
  }

  store.rebuildAggregatedReadingDays();
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  if (!loadJsonDocumentFromFile("RST", path, doc)) {
    return false;
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = false;

  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  auto appendReadingDays = [](std::vector<ReadingDayStats>& destination, JsonArray source) {
    for (JsonVariant value : source) {
      ReadingDayStats day;
      if (value.is<JsonObject>()) {
        JsonObject obj = value.as<JsonObject>();
        day.dayOrdinal = obj["dayOrdinal"] | static_cast<uint32_t>(0);
        day.readingMs = obj["readingMs"] | static_cast<uint64_t>(0);
      } else {
        day.dayOrdinal = value | static_cast<uint32_t>(0);
        day.readingMs = 0;
      }
      if (day.dayOrdinal != 0) {
        destination.push_back(day);
      }
    }
  };

  appendReadingDays(store.readingDays, doc["readingDays"].as<JsonArray>());
  if (formatVersion >= 2) {
    appendReadingDays(store.legacyReadingDays, doc["legacyReadingDays"].as<JsonArray>());
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    for (JsonObject sessionObj : doc["sessionLog"].as<JsonArray>()) {
      ReadingSessionLogEntry session;
      session.dayOrdinal = sessionObj["dayOrdinal"] | static_cast<uint32_t>(0);
      session.sessionMs = sessionObj["sessionMs"] | static_cast<uint32_t>(0);
      if (session.dayOrdinal != 0 && session.sessionMs != 0) {
        store.sessionLog.push_back(session);
      }
    }
  } else {
    store.dirty = true;
  }

  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject obj : books) {
    ReadingBookStats book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
    }
    for (JsonVariant value : obj["knownPaths"].as<JsonArray>()) {
      const std::string knownPath = value | std::string("");
      if (!knownPath.empty()) {
        book.knownPaths.push_back(knownPath);
      }
    }
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.chapterTitle = obj["chapterTitle"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | static_cast<uint64_t>(0);
    book.sessions = obj["sessions"] | static_cast<uint32_t>(0);
    book.lastSessionMs = obj["lastSessionMs"] | static_cast<uint32_t>(0);
    book.firstReadAt = obj["firstReadAt"] | static_cast<uint32_t>(0);
    book.lastReadAt = obj["lastReadAt"] | static_cast<uint32_t>(0);
    book.lastProgressPercent = obj["lastProgressPercent"] | static_cast<uint8_t>(0);
    book.chapterProgressPercent = obj["chapterProgressPercent"] | static_cast<uint8_t>(0);
    book.completed = obj["completed"] | false;
    if (formatVersion >= 2) {
      appendReadingDays(book.readingDays, obj["readingDays"].as<JsonArray>());
    }
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
    store.books.push_back(std::move(book));
  }

  store.rebuildAggregatedReadingDays();
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

// ---- AchievementsStore ----

bool JsonSettingsIO::saveAchievements(const AchievementsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  doc["accumulatedReadingMs"] = store.accumulatedReadingMs;
  doc["countedSessions"] = store.countedSessions;
  doc["totalBookmarksAdded"] = store.totalBookmarksAdded;
  doc["longestSessionMs"] = store.longestSessionMs;
  doc["goalDaysCount"] = store.goalDaysCount;
  doc["currentGoalStreak"] = store.currentGoalStreak;
  doc["maxGoalStreak"] = store.maxGoalStreak;
  doc["lastGoalDayOrdinal"] = store.lastGoalDayOrdinal;
  doc["resetDayOrdinal"] = store.resetDayOrdinal;
  doc["resetDayBaselineMs"] = store.resetDayBaselineMs;
  doc["lastProcessedSessionSerial"] = store.lastProcessedSessionSerial;

  JsonArray states = doc["states"].to<JsonArray>();
  for (const auto& state : store.states) {
    JsonObject obj = states.add<JsonObject>();
    obj["unlocked"] = state.unlocked;
    obj["unlockedAt"] = state.unlockedAt;
  }

  JsonArray startedBooks = doc["startedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.startedBooks) {
    startedBooks.add(pathValue);
  }

  JsonArray finishedBooks = doc["finishedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.finishedBooks) {
    finishedBooks.add(pathValue);
  }

  return saveJsonDocumentToFile("ACH", path, doc);
}

bool JsonSettingsIO::loadAchievements(AchievementsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("ACH", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  store.accumulatedReadingMs = doc["accumulatedReadingMs"] | static_cast<uint64_t>(0);
  store.countedSessions = doc["countedSessions"] | static_cast<uint32_t>(0);
  store.totalBookmarksAdded = doc["totalBookmarksAdded"] | static_cast<uint32_t>(0);
  store.longestSessionMs = doc["longestSessionMs"] | static_cast<uint32_t>(0);
  store.goalDaysCount = doc["goalDaysCount"] | static_cast<uint32_t>(0);
  store.currentGoalStreak = doc["currentGoalStreak"] | static_cast<uint32_t>(0);
  store.maxGoalStreak = doc["maxGoalStreak"] | static_cast<uint32_t>(0);
  store.lastGoalDayOrdinal = doc["lastGoalDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayOrdinal = doc["resetDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayBaselineMs = doc["resetDayBaselineMs"] | static_cast<uint64_t>(0);
  store.lastProcessedSessionSerial = doc["lastProcessedSessionSerial"] | static_cast<uint32_t>(0);

  JsonArray states = doc["states"].as<JsonArray>();
  size_t stateIndex = 0;
  for (JsonObject obj : states) {
    if (stateIndex >= store.states.size()) {
      break;
    }
    store.states[stateIndex].unlocked = obj["unlocked"] | false;
    store.states[stateIndex].unlockedAt = obj["unlockedAt"] | static_cast<uint32_t>(0);
    ++stateIndex;
  }

  for (JsonVariant value : doc["startedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.startedBooks.push_back(bookKey);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.finishedBooks.push_back(bookKey);
    }
  }

  return true;
}

bool JsonSettingsIO::loadAchievementsFromFile(AchievementsStore& store, const char* path) {
  JsonDocument doc;
  if (!loadJsonDocumentFromFile("ACH", path, doc)) {
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  store.accumulatedReadingMs = doc["accumulatedReadingMs"] | static_cast<uint64_t>(0);
  store.countedSessions = doc["countedSessions"] | static_cast<uint32_t>(0);
  store.totalBookmarksAdded = doc["totalBookmarksAdded"] | static_cast<uint32_t>(0);
  store.longestSessionMs = doc["longestSessionMs"] | static_cast<uint32_t>(0);
  store.goalDaysCount = doc["goalDaysCount"] | static_cast<uint32_t>(0);
  store.currentGoalStreak = doc["currentGoalStreak"] | static_cast<uint32_t>(0);
  store.maxGoalStreak = doc["maxGoalStreak"] | static_cast<uint32_t>(0);
  store.lastGoalDayOrdinal = doc["lastGoalDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayOrdinal = doc["resetDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayBaselineMs = doc["resetDayBaselineMs"] | static_cast<uint64_t>(0);
  store.lastProcessedSessionSerial = doc["lastProcessedSessionSerial"] | static_cast<uint32_t>(0);

  JsonArray states = doc["states"].as<JsonArray>();
  size_t stateIndex = 0;
  for (JsonObject obj : states) {
    if (stateIndex >= store.states.size()) {
      break;
    }
    store.states[stateIndex].unlocked = obj["unlocked"] | false;
    store.states[stateIndex].unlockedAt = obj["unlockedAt"] | static_cast<uint32_t>(0);
    ++stateIndex;
  }

  for (JsonVariant value : doc["startedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.startedBooks.push_back(bookKey);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.finishedBooks.push_back(bookKey);
    }
  }

  return true;
}
