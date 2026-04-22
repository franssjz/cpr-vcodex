#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Stream.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "AchievementsStore.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SettingsList.h"
#include "WifiCredentialStore.h"
#include "util/BookIdentity.h"
#include "util/CprVcodexLogs.h"
#include "util/ShortcutRegistry.h"
#include "util/TimeZoneRegistry.h"

namespace {
constexpr uint8_t FONT_FAMILY_SCHEMA_VERSION = 2;
constexpr uint8_t FONT_SIZE_SCHEMA_VERSION = 2;
constexpr uint8_t UI_THEME_SCHEMA_VERSION = 2;
constexpr uint8_t FLASHCARD_STUDY_MODE_SCHEMA_VERSION = 2;

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
  const std::string targetPath = path ? path : "";
  const std::string tempPath = targetPath + ".tmp";

  if (targetPath.empty()) {
    LOG_ERR(moduleName, "Missing JSON path for write");
    CprVcodexLogs::appendEvent(moduleName, "Missing JSON path for write");
    return false;
  }

  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite(moduleName, tempPath.c_str(), file)) {
    LOG_ERR(moduleName, "Could not open JSON file for write: %s", tempPath.c_str());
    CprVcodexLogs::appendEvent(moduleName, std::string("Could not open JSON temp file for write: ") + tempPath);
    return false;
  }

  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  if (written == 0) {
    Storage.remove(tempPath.c_str());
    CprVcodexLogs::appendEvent(moduleName, std::string("serializeJson wrote 0 bytes for ") + targetPath);
    return false;
  }

  if (Storage.exists(targetPath.c_str()) && !Storage.remove(targetPath.c_str())) {
    Storage.remove(tempPath.c_str());
    LOG_ERR(moduleName, "Could not remove JSON file before replace: %s", targetPath.c_str());
    CprVcodexLogs::appendEvent(moduleName,
                               std::string("Could not remove JSON file before replace: ") + targetPath);
    return false;
  }

  if (!Storage.rename(tempPath.c_str(), targetPath.c_str())) {
    Storage.remove(tempPath.c_str());
    LOG_ERR(moduleName, "Could not rename JSON temp file to final path: %s", targetPath.c_str());
    CprVcodexLogs::appendEvent(moduleName,
                               std::string("Could not rename JSON temp file to final path: ") + targetPath);
    return false;
  }

  return true;
}

bool loadJsonDocumentFromFile(const char* moduleName, const char* path, JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for read: %s", path);
    if (Storage.exists(path)) {
      CprVcodexLogs::appendEvent(moduleName, std::string("Could not open JSON file for read: ") + path);
    }
    return false;
  }

  HalFileStream stream(file);
  auto error = deserializeJson(doc, stream);
  file.close();
  if (error) {
    LOG_ERR(moduleName, "JSON parse error: %s", error.c_str());
    const std::string reportBody = std::string("File: ") + path + "\nModule: " + moduleName +
                                   "\nError: " + error.c_str() + "\n";
    std::string outPath;
    if (CprVcodexLogs::writeReport("json_error", reportBody, &outPath)) {
      CprVcodexLogs::appendEvent(moduleName, std::string("Saved JSON parse error report to ") + outPath);
    }
    return false;
  }
  return true;
}

uint8_t migrateStoredUiTheme(const uint8_t rawUiTheme, const uint8_t schemaVersion, const uint8_t currentDefault,
                             bool* needsResave) {
  if (schemaVersion >= UI_THEME_SCHEMA_VERSION) {
    const uint8_t clampedTheme =
        rawUiTheme < static_cast<uint8_t>(CrossPointSettings::UI_THEME_COUNT) ? rawUiTheme : currentDefault;
    if (clampedTheme != rawUiTheme && needsResave) *needsResave = true;
    return clampedTheme;
  }

  // Legacy/theme-consolidation migration:
  // - 0 (Classic) -> Lyra
  // - 2/3 (Extended/Custom) -> Lyra vCodex
  // - 1 is ambiguous: in the current 2-theme schema it already means Lyra vCodex,
  //   while in older schemas it meant Lyra. Prefer preserving the newer stored value.
  uint8_t migratedTheme = currentDefault;
  switch (rawUiTheme) {
    case 0:
      migratedTheme = CrossPointSettings::LYRA;
      break;
    case 1:
      migratedTheme = CrossPointSettings::LYRA_CUSTOM;
      break;
    case 2:
    case 3:
      migratedTheme = CrossPointSettings::LYRA_CUSTOM;
      break;
    default:
      migratedTheme = currentDefault;
      break;
  }

  if (migratedTheme != rawUiTheme && needsResave) *needsResave = true;
  return migratedTheme;
}

uint8_t migrateStoredFlashcardStudyMode(const uint8_t rawMode, const uint8_t schemaVersion, const uint8_t currentDefault,
                                        bool* needsResave) {
  if (schemaVersion >= FLASHCARD_STUDY_MODE_SCHEMA_VERSION) {
    const uint8_t clampedMode = rawMode < static_cast<uint8_t>(CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT)
                                    ? rawMode
                                    : currentDefault;
    if (clampedMode != rawMode && needsResave) *needsResave = true;
    return clampedMode;
  }

  // Legacy mapping before Due existed:
  // - 0 -> Scheduled
  // - 1 -> Infinite
  uint8_t migratedMode = currentDefault;
  switch (rawMode) {
    case 0:
      migratedMode = CrossPointSettings::FLASHCARD_STUDY_SCHEDULED;
      break;
    case 1:
      migratedMode = CrossPointSettings::FLASHCARD_STUDY_INFINITE;
      break;
    default:
      migratedMode = currentDefault;
      break;
  }

  if (migratedMode != rawMode && needsResave) *needsResave = true;
  return migratedMode;
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

bool loadSettingsDirect(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };
  auto loadToggle = [&](const char* key, uint8_t& field) {
    field = clamp(doc[key] | field, static_cast<uint8_t>(2), field);
  };
  auto loadEnum = [&](const char* key, uint8_t& field, const uint8_t count) { field = clamp(doc[key] | field, count, field); };
  auto loadValue = [&](const char* key, uint8_t& field, const uint8_t minValue, const uint8_t maxValue) {
    uint8_t value = doc[key] | field;
    if (value < minValue) {
      value = minValue;
    } else if (value > maxValue) {
      value = maxValue;
    }
    field = value;
  };
  auto loadString = [&](const char* key, char* dest, const size_t maxLen) {
    const std::string value = doc[key] | std::string(dest);
    strncpy(dest, value.c_str(), maxLen - 1);
    dest[maxLen - 1] = '\0';
  };

  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  loadEnum("sleepScreen", s.sleepScreen, CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
  loadEnum("sleepScreenCoverMode", s.sleepScreenCoverMode, CrossPointSettings::SLEEP_SCREEN_COVER_MODE_COUNT);
  loadEnum("sleepScreenCoverFilter", s.sleepScreenCoverFilter, CrossPointSettings::SLEEP_SCREEN_COVER_FILTER_COUNT);
  loadEnum("hideBatteryPercentage", s.hideBatteryPercentage, CrossPointSettings::HIDE_BATTERY_PERCENTAGE_COUNT);
  loadEnum("refreshFrequency", s.refreshFrequency, CrossPointSettings::REFRESH_FREQUENCY_COUNT);
  {
    const uint8_t rawUiTheme = doc["uiTheme"] | s.uiTheme;
    const uint8_t uiThemeSchemaVersion = doc["uiThemeSchemaVersion"] | static_cast<uint8_t>(0);
    s.uiTheme = migrateStoredUiTheme(rawUiTheme, uiThemeSchemaVersion, s.uiTheme, needsResave);
  }
  loadToggle("fadingFix", s.fadingFix);
  loadToggle("darkMode", s.darkMode);

  const uint8_t rawFontFamily = doc["fontFamily"] | s.fontFamily;
  if (rawFontFamily >= static_cast<uint8_t>(CrossPointSettings::FONT_FAMILY_COUNT)) {
    s.fontFamily = CrossPointSettings::BOOKERLY;
    if (needsResave) *needsResave = true;
  } else {
    s.fontFamily = rawFontFamily;
  }

  loadEnum("fontSize", s.fontSize, CrossPointSettings::FONT_SIZE_COUNT);
  const uint8_t fontSizeSchemaVersion = doc["fontSizeSchemaVersion"] | static_cast<uint8_t>(0);
  if (fontSizeSchemaVersion < FONT_SIZE_SCHEMA_VERSION && !doc["fontSize"].isNull()) {
    const uint8_t legacyFontSize = doc["fontSize"] | static_cast<uint8_t>(CrossPointSettings::MEDIUM - 1);
    if (legacyFontSize < static_cast<uint8_t>(CrossPointSettings::EXTRA_LARGE)) {
      s.fontSize = static_cast<uint8_t>(legacyFontSize + 1);
      if (needsResave) *needsResave = true;
    }
  }

  loadEnum("lineSpacing", s.lineSpacing, CrossPointSettings::LINE_COMPRESSION_COUNT);
  loadValue("screenMargin", s.screenMargin, 5, 40);
  loadEnum("paragraphAlignment", s.paragraphAlignment, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
  loadToggle("embeddedStyle", s.embeddedStyle);
  loadToggle("hyphenationEnabled", s.hyphenationEnabled);
  loadToggle("bionicReading", s.bionicReading);
  loadEnum("orientation", s.orientation, CrossPointSettings::ORIENTATION_COUNT);
  loadToggle("extraParagraphSpacing", s.extraParagraphSpacing);
  loadToggle("textAntiAliasing", s.textAntiAliasing);
  loadEnum("textDarkness", s.textDarkness, CrossPointSettings::TEXT_DARKNESS_COUNT);
  loadEnum("readerRefreshMode", s.readerRefreshMode, CrossPointSettings::READER_REFRESH_MODE_COUNT);
  loadEnum("imageRendering", s.imageRendering, CrossPointSettings::IMAGE_RENDERING_COUNT);

  loadEnum("sideButtonLayout", s.sideButtonLayout, CrossPointSettings::SIDE_BUTTON_LAYOUT_COUNT);
  loadToggle("longPressChapterSkip", s.longPressChapterSkip);
  loadEnum("shortPwrBtn", s.shortPwrBtn, CrossPointSettings::SHORT_PWRBTN_COUNT);
  loadEnum("sleepTimeout", s.sleepTimeout, CrossPointSettings::SLEEP_TIMEOUT_COUNT);
  loadToggle("showHiddenFiles", s.showHiddenFiles);

  loadString("opdsServerUrl", s.opdsServerUrl, sizeof(s.opdsServerUrl));
  loadString("opdsUsername", s.opdsUsername, sizeof(s.opdsUsername));
  {
    bool ok = false;
    std::string password = obfuscation::deobfuscateFromBase64(doc["opdsPassword_obf"] | "", &ok);
    if (!ok || password.empty()) {
      password = doc["opdsPassword"] | std::string(s.opdsPassword);
      if (password != s.opdsPassword && needsResave) *needsResave = true;
    }
    strncpy(s.opdsPassword, password.c_str(), sizeof(s.opdsPassword) - 1);
    s.opdsPassword[sizeof(s.opdsPassword) - 1] = '\0';
  }

  loadToggle("statusBarChapterPageCount", s.statusBarChapterPageCount);
  loadToggle("statusBarBookProgressPercentage", s.statusBarBookProgressPercentage);
  loadEnum("statusBarProgressBar", s.statusBarProgressBar, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_COUNT);
  loadEnum("statusBarProgressBarThickness", s.statusBarProgressBarThickness,
           CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT);
  loadEnum("statusBarTitle", s.statusBarTitle, CrossPointSettings::STATUS_BAR_TITLE_COUNT);
  loadToggle("statusBarBattery", s.statusBarBattery);

  using S = CrossPointSettings;
  s.frontButtonBack =
      clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM, S::FRONT_BUTTON_HARDWARE_COUNT,
                               S::FRONT_HW_CONFIRM);
  s.frontButtonLeft =
      clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  s.homeCarouselSource =
      clamp(doc["homeCarouselSource"] | s.homeCarouselSource, S::HOME_CAROUSEL_SOURCE_COUNT, s.homeCarouselSource);
  s.displayDay = clamp(doc["displayDay"] | s.displayDay, static_cast<uint8_t>(2), s.displayDay);
  s.autoSyncDay = clamp(doc["autoSyncDay"] | s.autoSyncDay, static_cast<uint8_t>(2), s.autoSyncDay);
  s.syncDayWifiChoice =
      clamp(doc["syncDayWifiChoice"] | s.syncDayWifiChoice, S::SYNC_DAY_WIFI_CHOICE_COUNT, s.syncDayWifiChoice);
  s.syncDayReminderStarts =
      clamp(doc["syncDayReminderStarts"] | s.syncDayReminderStarts, S::SYNC_DAY_REMINDER_STARTS_COUNT,
            s.syncDayReminderStarts);
  {
    const std::string sleepDirectory = doc["sleepDirectory"] | std::string("");
    strncpy(s.sleepDirectory, sleepDirectory.c_str(), sizeof(s.sleepDirectory) - 1);
    s.sleepDirectory[sizeof(s.sleepDirectory) - 1] = '\0';
  }
  s.sleepImageOrder = clamp(doc["sleepImageOrder"] | static_cast<uint8_t>(S::SLEEP_IMAGE_SHUFFLE),
                            S::SLEEP_IMAGE_ORDER_COUNT, S::SLEEP_IMAGE_SHUFFLE);
  s.timeZonePreset =
      TimeZoneRegistry::clampPresetIndex(doc["timeZonePreset"] | TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);
  s.dateFormat = clamp(doc["dateFormat"] | s.dateFormat, S::DATE_FORMAT_COUNT, s.dateFormat);
  s.dailyGoalTarget = clamp(doc["dailyGoalTarget"] | s.dailyGoalTarget, S::DAILY_GOAL_TARGET_COUNT, s.dailyGoalTarget);
  {
    const uint8_t rawFlashcardStudyMode = doc["flashcardStudyMode"] | s.flashcardStudyMode;
    const uint8_t flashcardStudyModeSchemaVersion = doc["flashcardStudyModeSchemaVersion"] | static_cast<uint8_t>(0);
    s.flashcardStudyMode = migrateStoredFlashcardStudyMode(rawFlashcardStudyMode, flashcardStudyModeSchemaVersion,
                                                           s.flashcardStudyMode, needsResave);
  }
  s.flashcardSessionSize =
      clamp(doc["flashcardSessionSize"] | s.flashcardSessionSize, S::FLASHCARD_SESSION_SIZE_COUNT,
            s.flashcardSessionSize);
  s.showStatsAfterReading =
      clamp(doc["showStatsAfterReading"] | s.showStatsAfterReading, static_cast<uint8_t>(2), s.showStatsAfterReading);
  s.achievementsEnabled =
      clamp(doc["achievementsEnabled"] | s.achievementsEnabled, static_cast<uint8_t>(2), s.achievementsEnabled);
  s.achievementPopups =
      clamp(doc["achievementPopups"] | s.achievementPopups, static_cast<uint8_t>(2), s.achievementPopups);

  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.appsHubShortcutOrder = clamp(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount,
                                 s.appsHubShortcutOrder);
  s.browseFilesShortcut =
      clamp(doc["browseFilesShortcut"] | s.browseFilesShortcut, shortcutLocationCount, s.browseFilesShortcut);
  s.browseFilesShortcutOrder = clamp(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount,
                                     s.browseFilesShortcutOrder);
  s.statsShortcut = clamp(doc["statsShortcut"] | s.statsShortcut, shortcutLocationCount, s.statsShortcut);
  s.statsShortcutOrder =
      clamp(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcut = clamp(doc["syncDayShortcut"] | s.syncDayShortcut, shortcutLocationCount, s.syncDayShortcut);
  s.syncDayShortcutOrder =
      clamp(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcut = clamp(doc["settingsShortcut"] | s.settingsShortcut, shortcutLocationCount, s.settingsShortcut);
  s.settingsShortcutOrder =
      clamp(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcut =
      clamp(doc["readingStatsShortcut"] | s.readingStatsShortcut, shortcutLocationCount, s.readingStatsShortcut);
  s.readingStatsShortcutOrder = clamp(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder,
                                      shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcut = clamp(doc["readingHeatmapShortcut"] | s.readingHeatmapShortcut, shortcutLocationCount,
                                   s.readingHeatmapShortcut);
  s.readingHeatmapShortcutOrder = clamp(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder,
                                        shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcut = clamp(doc["readingProfileShortcut"] | s.readingProfileShortcut, shortcutLocationCount,
                                   s.readingProfileShortcut);
  s.readingProfileShortcutOrder = clamp(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder,
                                        shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcut =
      clamp(doc["achievementsShortcut"] | s.achievementsShortcut, shortcutLocationCount, s.achievementsShortcut);
  s.achievementsShortcutOrder = clamp(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder,
                                      shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcut = clamp(doc["ifFoundShortcut"] | s.ifFoundShortcut, shortcutLocationCount, s.ifFoundShortcut);
  s.ifFoundShortcutOrder =
      clamp(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcut = clamp(doc["readMeShortcut"] | s.readMeShortcut, shortcutLocationCount, s.readMeShortcut);
  s.readMeShortcutOrder =
      clamp(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcut =
      clamp(doc["recentBooksShortcut"] | s.recentBooksShortcut, shortcutLocationCount, s.recentBooksShortcut);
  s.recentBooksShortcutOrder = clamp(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount,
                                     s.recentBooksShortcutOrder);
  s.bookmarksShortcut =
      clamp(doc["bookmarksShortcut"] | s.bookmarksShortcut, shortcutLocationCount, s.bookmarksShortcut);
  s.bookmarksShortcutOrder =
      clamp(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.favoritesShortcut =
      clamp(doc["favoritesShortcut"] | s.favoritesShortcut, shortcutLocationCount, s.favoritesShortcut);
  s.favoritesShortcutOrder =
      clamp(doc["favoritesShortcutOrder"] | s.favoritesShortcutOrder, shortcutOrderCount, s.favoritesShortcutOrder);
  s.flashcardsShortcut =
      clamp(doc["flashcardsShortcut"] | s.flashcardsShortcut, shortcutLocationCount, s.flashcardsShortcut);
  s.flashcardsShortcutOrder =
      clamp(doc["flashcardsShortcutOrder"] | s.flashcardsShortcutOrder, shortcutOrderCount, s.flashcardsShortcutOrder);
  s.fileTransferShortcut =
      clamp(doc["fileTransferShortcut"] | s.fileTransferShortcut, shortcutLocationCount, s.fileTransferShortcut);
  s.fileTransferShortcutOrder = clamp(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder,
                                      shortcutOrderCount, s.fileTransferShortcutOrder);
  s.sleepShortcut = clamp(doc["sleepShortcut"] | s.sleepShortcut, shortcutLocationCount, s.sleepShortcut);
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
  s.favoritesShortcutVisible =
      clamp(doc["favoritesShortcutVisible"] | s.favoritesShortcutVisible, static_cast<uint8_t>(2),
            s.favoritesShortcutVisible);
  s.flashcardsShortcutVisible =
      clamp(doc["flashcardsShortcutVisible"] | s.flashcardsShortcutVisible, static_cast<uint8_t>(2),
            s.flashcardsShortcutVisible);
  s.fileTransferShortcutVisible =
      clamp(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible, static_cast<uint8_t>(2),
            s.fileTransferShortcutVisible);
  s.sleepShortcutVisible =
      clamp(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, static_cast<uint8_t>(2), s.sleepShortcutVisible);

  normalizeShortcutOrderSettings(s);
  CrossPointSettings::validateFrontButtonMapping(s);

  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  JsonArray recentArr = doc["recentSleepImages"].to<JsonArray>();
  for (int i = 0; i < CrossPointState::SLEEP_RECENT_COUNT; i++) recentArr.add(s.recentSleepImages[i]);
  doc["recentSleepPos"] = s.recentSleepPos;
  doc["recentSleepFill"] = s.recentSleepFill;
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
    CprVcodexLogs::appendEvent("CPS", std::string("Settings JSON parse error: ") + error.c_str());
    return false;
  }

  s.openEpubPath = doc["openEpubPath"] | std::string("");
  memset(s.recentSleepImages, 0, sizeof(s.recentSleepImages));
  JsonArrayConst recentArr = doc["recentSleepImages"];
  const int actualCount = recentArr.isNull() ? 0
                                             : std::min(static_cast<int>(recentArr.size()),
                                                        static_cast<int>(CrossPointState::SLEEP_RECENT_COUNT));
  for (int i = 0; i < actualCount; i++) s.recentSleepImages[i] = recentArr[i] | static_cast<uint16_t>(0);
  s.recentSleepPos = doc["recentSleepPos"] | static_cast<uint8_t>(0);
  if (s.recentSleepPos >= CrossPointState::SLEEP_RECENT_COUNT)
    s.recentSleepPos = actualCount > 0 ? s.recentSleepPos % CrossPointState::SLEEP_RECENT_COUNT : 0;
  s.recentSleepFill = doc["recentSleepFill"] | static_cast<uint8_t>(0);
  s.recentSleepFill = static_cast<uint8_t>(std::min(static_cast<int>(s.recentSleepFill), actualCount));
  if (s.recentSleepFill == 0 && !doc["lastSleepImage"].isNull()) {
    const uint8_t legacy = doc["lastSleepImage"] | static_cast<uint8_t>(UINT8_MAX);
    if (legacy != UINT8_MAX) s.pushRecentSleep(static_cast<uint16_t>(legacy));
  }
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

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["uiTheme"] = s.uiTheme;
  doc["uiThemeSchemaVersion"] = UI_THEME_SCHEMA_VERSION;
  doc["fadingFix"] = s.fadingFix;
  doc["darkMode"] = s.darkMode;

  doc["fontFamily"] = s.fontFamily;
  doc["fontFamilySchemaVersion"] = FONT_FAMILY_SCHEMA_VERSION;
  doc["fontSize"] = s.fontSize;
  doc["fontSizeSchemaVersion"] = FONT_SIZE_SCHEMA_VERSION;
  doc["lineSpacing"] = s.lineSpacing;
  doc["screenMargin"] = s.screenMargin;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["bionicReading"] = s.bionicReading;
  doc["orientation"] = s.orientation;
  doc["extraParagraphSpacing"] = s.extraParagraphSpacing;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["textDarkness"] = s.textDarkness;
  doc["readerRefreshMode"] = s.readerRefreshMode;
  doc["imageRendering"] = s.imageRendering;

  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["longPressChapterSkip"] = s.longPressChapterSkip;
  doc["shortPwrBtn"] = s.shortPwrBtn;

  doc["sleepTimeout"] = s.sleepTimeout;
  doc["showHiddenFiles"] = s.showHiddenFiles;

  doc["homeCarouselSource"] = s.homeCarouselSource;
  doc["displayDay"] = s.displayDay;
  doc["syncDayWifiChoice"] = s.syncDayWifiChoice;
  doc["syncDayReminderStarts"] = s.syncDayReminderStarts;
  doc["dateFormat"] = s.dateFormat;
  doc["dailyGoalTarget"] = s.dailyGoalTarget;
  doc["flashcardStudyModeSchemaVersion"] = FLASHCARD_STUDY_MODE_SCHEMA_VERSION;
  doc["flashcardStudyMode"] = s.flashcardStudyMode;
  doc["flashcardSessionSize"] = s.flashcardSessionSize;
  doc["showStatsAfterReading"] = s.showStatsAfterReading;
  doc["achievementsEnabled"] = s.achievementsEnabled;
  doc["achievementPopups"] = s.achievementPopups;

  doc["opdsServerUrl"] = s.opdsServerUrl;
  doc["opdsUsername"] = s.opdsUsername;
  doc["opdsPassword_obf"] = obfuscation::obfuscateToBase64(s.opdsPassword);

  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["autoSyncDay"] = s.autoSyncDay;
  doc["sleepDirectory"] = s.sleepDirectory;
  doc["sleepImageOrder"] = s.sleepImageOrder;
  doc["timeZonePreset"] = TimeZoneRegistry::clampPresetIndex(s.timeZonePreset);
  doc["appsHubShortcutOrder"] = s.appsHubShortcutOrder;
  doc["browseFilesShortcut"] = s.browseFilesShortcut;
  doc["browseFilesShortcutOrder"] = s.browseFilesShortcutOrder;
  doc["statsShortcut"] = s.statsShortcut;
  doc["statsShortcutOrder"] = s.statsShortcutOrder;
  doc["syncDayShortcut"] = s.syncDayShortcut;
  doc["syncDayShortcutOrder"] = s.syncDayShortcutOrder;
  doc["settingsShortcut"] = s.settingsShortcut;
  doc["settingsShortcutOrder"] = s.settingsShortcutOrder;
  doc["readingStatsShortcut"] = s.readingStatsShortcut;
  doc["readingStatsShortcutOrder"] = s.readingStatsShortcutOrder;
  doc["readingHeatmapShortcut"] = s.readingHeatmapShortcut;
  doc["readingHeatmapShortcutOrder"] = s.readingHeatmapShortcutOrder;
  doc["readingProfileShortcut"] = s.readingProfileShortcut;
  doc["readingProfileShortcutOrder"] = s.readingProfileShortcutOrder;
  doc["achievementsShortcut"] = s.achievementsShortcut;
  doc["achievementsShortcutOrder"] = s.achievementsShortcutOrder;
  doc["ifFoundShortcut"] = s.ifFoundShortcut;
  doc["ifFoundShortcutOrder"] = s.ifFoundShortcutOrder;
  doc["readMeShortcut"] = s.readMeShortcut;
  doc["readMeShortcutOrder"] = s.readMeShortcutOrder;
  doc["recentBooksShortcut"] = s.recentBooksShortcut;
  doc["recentBooksShortcutOrder"] = s.recentBooksShortcutOrder;
  doc["bookmarksShortcut"] = s.bookmarksShortcut;
  doc["bookmarksShortcutOrder"] = s.bookmarksShortcutOrder;
  doc["favoritesShortcut"] = s.favoritesShortcut;
  doc["favoritesShortcutOrder"] = s.favoritesShortcutOrder;
  doc["flashcardsShortcut"] = s.flashcardsShortcut;
  doc["flashcardsShortcutOrder"] = s.flashcardsShortcutOrder;
  doc["fileTransferShortcut"] = s.fileTransferShortcut;
  doc["fileTransferShortcutOrder"] = s.fileTransferShortcutOrder;
  doc["sleepShortcut"] = s.sleepShortcut;
  doc["sleepShortcutOrder"] = s.sleepShortcutOrder;
  doc["browseFilesShortcutVisible"] = s.browseFilesShortcutVisible;
  doc["statsShortcutVisible"] = s.statsShortcutVisible;
  doc["syncDayShortcutVisible"] = s.syncDayShortcutVisible;
  doc["settingsShortcutVisible"] = s.settingsShortcutVisible;
  doc["readingStatsShortcutVisible"] = s.readingStatsShortcutVisible;
  doc["readingHeatmapShortcutVisible"] = s.readingHeatmapShortcutVisible;
  doc["readingProfileShortcutVisible"] = s.readingProfileShortcutVisible;
  doc["achievementsShortcutVisible"] = s.achievementsShortcutVisible;
  doc["ifFoundShortcutVisible"] = s.ifFoundShortcutVisible;
  doc["readMeShortcutVisible"] = s.readMeShortcutVisible;
  doc["recentBooksShortcutVisible"] = s.recentBooksShortcutVisible;
  doc["bookmarksShortcutVisible"] = s.bookmarksShortcutVisible;
  doc["favoritesShortcutVisible"] = s.favoritesShortcutVisible;
  doc["flashcardsShortcutVisible"] = s.flashcardsShortcutVisible;
  doc["fileTransferShortcutVisible"] = s.fileTransferShortcutVisible;
  doc["sleepShortcutVisible"] = s.sleepShortcutVisible;

  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    CprVcodexLogs::appendEvent("CPS", std::string("State JSON parse error: ") + error.c_str());
    return false;
  }

  return loadSettingsDirect(s, doc, needsResave);

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  // Legacy migration: if statusBarChapterPageCount is absent this is a pre-refactor settings file.
  // Populate s with migrated values now so the generic loop below picks them up as defaults and clamps them.
  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  for (const auto& info : getSettingsList()) {
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

  const uint8_t rawFontFamily = doc["fontFamily"] | s.fontFamily;
  if (rawFontFamily >= static_cast<uint8_t>(CrossPointSettings::FONT_FAMILY_COUNT)) {
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
  s.homeCarouselSource =
      clamp(doc["homeCarouselSource"] | s.homeCarouselSource, S::HOME_CAROUSEL_SOURCE_COUNT, s.homeCarouselSource);
  s.displayDay = clamp(doc["displayDay"] | s.displayDay, static_cast<uint8_t>(2), s.displayDay);
  s.autoSyncDay = clamp(doc["autoSyncDay"] | s.autoSyncDay, static_cast<uint8_t>(2), s.autoSyncDay);
  s.syncDayWifiChoice =
      clamp(doc["syncDayWifiChoice"] | s.syncDayWifiChoice, S::SYNC_DAY_WIFI_CHOICE_COUNT, s.syncDayWifiChoice);
  s.syncDayReminderStarts =
      clamp(doc["syncDayReminderStarts"] | s.syncDayReminderStarts, S::SYNC_DAY_REMINDER_STARTS_COUNT,
            s.syncDayReminderStarts);
  {
    const std::string sleepDirectory = doc["sleepDirectory"] | std::string("");
    strncpy(s.sleepDirectory, sleepDirectory.c_str(), sizeof(s.sleepDirectory) - 1);
    s.sleepDirectory[sizeof(s.sleepDirectory) - 1] = '\0';
  }
  s.sleepImageOrder = clamp(doc["sleepImageOrder"] | static_cast<uint8_t>(S::SLEEP_IMAGE_SHUFFLE),
                            S::SLEEP_IMAGE_ORDER_COUNT, S::SLEEP_IMAGE_SHUFFLE);
  s.timeZonePreset =
      TimeZoneRegistry::clampPresetIndex(doc["timeZonePreset"] | TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);
  s.dateFormat = clamp(doc["dateFormat"] | s.dateFormat, S::DATE_FORMAT_COUNT, s.dateFormat);
  s.dailyGoalTarget = clamp(doc["dailyGoalTarget"] | s.dailyGoalTarget, S::DAILY_GOAL_TARGET_COUNT, s.dailyGoalTarget);
  {
    const uint8_t rawFlashcardStudyMode = doc["flashcardStudyMode"] | s.flashcardStudyMode;
    const uint8_t flashcardStudyModeSchemaVersion = doc["flashcardStudyModeSchemaVersion"] | static_cast<uint8_t>(0);
    s.flashcardStudyMode = migrateStoredFlashcardStudyMode(rawFlashcardStudyMode, flashcardStudyModeSchemaVersion,
                                                           s.flashcardStudyMode, nullptr);
  }
  s.flashcardSessionSize =
      clamp(doc["flashcardSessionSize"] | s.flashcardSessionSize, S::FLASHCARD_SESSION_SIZE_COUNT,
            s.flashcardSessionSize);
  s.showStatsAfterReading =
      clamp(doc["showStatsAfterReading"] | s.showStatsAfterReading, static_cast<uint8_t>(2), s.showStatsAfterReading);
  s.achievementsEnabled =
      clamp(doc["achievementsEnabled"] | s.achievementsEnabled, static_cast<uint8_t>(2), s.achievementsEnabled);
  s.achievementPopups =
      clamp(doc["achievementPopups"] | s.achievementPopups, static_cast<uint8_t>(2), s.achievementPopups);

  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.appsHubShortcutOrder = clamp(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount,
                                 s.appsHubShortcutOrder);
  s.browseFilesShortcut =
      clamp(doc["browseFilesShortcut"] | s.browseFilesShortcut, shortcutLocationCount, s.browseFilesShortcut);
  s.browseFilesShortcutOrder = clamp(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount,
                                     s.browseFilesShortcutOrder);
  s.statsShortcut = clamp(doc["statsShortcut"] | s.statsShortcut, shortcutLocationCount, s.statsShortcut);
  s.statsShortcutOrder =
      clamp(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcut = clamp(doc["syncDayShortcut"] | s.syncDayShortcut, shortcutLocationCount, s.syncDayShortcut);
  s.syncDayShortcutOrder =
      clamp(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcut = clamp(doc["settingsShortcut"] | s.settingsShortcut, shortcutLocationCount, s.settingsShortcut);
  s.settingsShortcutOrder =
      clamp(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcut =
      clamp(doc["readingStatsShortcut"] | s.readingStatsShortcut, shortcutLocationCount, s.readingStatsShortcut);
  s.readingStatsShortcutOrder = clamp(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder,
                                      shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcut = clamp(doc["readingHeatmapShortcut"] | s.readingHeatmapShortcut, shortcutLocationCount,
                                   s.readingHeatmapShortcut);
  s.readingHeatmapShortcutOrder = clamp(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder,
                                        shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcut = clamp(doc["readingProfileShortcut"] | s.readingProfileShortcut, shortcutLocationCount,
                                   s.readingProfileShortcut);
  s.readingProfileShortcutOrder = clamp(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder,
                                        shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcut =
      clamp(doc["achievementsShortcut"] | s.achievementsShortcut, shortcutLocationCount, s.achievementsShortcut);
  s.achievementsShortcutOrder = clamp(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder,
                                      shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcut = clamp(doc["ifFoundShortcut"] | s.ifFoundShortcut, shortcutLocationCount, s.ifFoundShortcut);
  s.ifFoundShortcutOrder =
      clamp(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcut = clamp(doc["readMeShortcut"] | s.readMeShortcut, shortcutLocationCount, s.readMeShortcut);
  s.readMeShortcutOrder =
      clamp(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcut =
      clamp(doc["recentBooksShortcut"] | s.recentBooksShortcut, shortcutLocationCount, s.recentBooksShortcut);
  s.recentBooksShortcutOrder = clamp(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount,
                                     s.recentBooksShortcutOrder);
  s.bookmarksShortcut =
      clamp(doc["bookmarksShortcut"] | s.bookmarksShortcut, shortcutLocationCount, s.bookmarksShortcut);
  s.bookmarksShortcutOrder =
      clamp(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.favoritesShortcut =
      clamp(doc["favoritesShortcut"] | s.favoritesShortcut, shortcutLocationCount, s.favoritesShortcut);
  s.favoritesShortcutOrder =
      clamp(doc["favoritesShortcutOrder"] | s.favoritesShortcutOrder, shortcutOrderCount, s.favoritesShortcutOrder);
  s.flashcardsShortcut =
      clamp(doc["flashcardsShortcut"] | s.flashcardsShortcut, shortcutLocationCount, s.flashcardsShortcut);
  s.flashcardsShortcutOrder =
      clamp(doc["flashcardsShortcutOrder"] | s.flashcardsShortcutOrder, shortcutOrderCount, s.flashcardsShortcutOrder);
  s.fileTransferShortcut =
      clamp(doc["fileTransferShortcut"] | s.fileTransferShortcut, shortcutLocationCount, s.fileTransferShortcut);
  s.fileTransferShortcutOrder = clamp(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder,
                                      shortcutOrderCount, s.fileTransferShortcutOrder);
  s.sleepShortcut = clamp(doc["sleepShortcut"] | s.sleepShortcut, shortcutLocationCount, s.sleepShortcut);
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
  s.favoritesShortcutVisible =
      clamp(doc["favoritesShortcutVisible"] | s.favoritesShortcutVisible, static_cast<uint8_t>(2),
            s.favoritesShortcutVisible);
  s.flashcardsShortcutVisible =
      clamp(doc["flashcardsShortcutVisible"] | s.flashcardsShortcutVisible, static_cast<uint8_t>(2),
            s.flashcardsShortcutVisible);
  s.fileTransferShortcutVisible =
      clamp(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible, static_cast<uint8_t>(2),
            s.fileTransferShortcutVisible);
  s.sleepShortcutVisible =
      clamp(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, static_cast<uint8_t>(2), s.sleepShortcutVisible);

  normalizeShortcutOrderSettings(s);
  CrossPointSettings::validateFrontButtonMapping(s);

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
    CprVcodexLogs::appendEvent("KRS", std::string("KOReader JSON parse error: ") + error.c_str());
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
    CprVcodexLogs::appendEvent("WCS", std::string("WiFi JSON parse error: ") + error.c_str());
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
    CprVcodexLogs::appendEvent("RBS", std::string("Recent books JSON parse error: ") + error.c_str());
    return false;
  }

  store.recentBooks.clear();
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);
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

// ---- FavoritesStore ----

bool JsonSettingsIO::saveFavorites(const FavoritesStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 1;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  return saveJsonDocumentToFile("FAV", path, doc);
}

bool JsonSettingsIO::loadFavorites(FavoritesStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("FAV", "JSON parse error: %s", error.c_str());
    CprVcodexLogs::appendEvent("FAV", std::string("Favorites JSON parse error: ") + error.c_str());
    return false;
  }

  store.favoriteBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    FavoriteBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.favoriteBooks.push_back(book);
  }

  store.normalizeBooks();
  LOG_DBG("FAV", "Favorites loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- ReadingStatsStore ----

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 5;

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
    obj["completedAt"] = book.completedAt;
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
    CprVcodexLogs::appendEvent("RST", std::string("Reading stats JSON parse error: ") + error.c_str());
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
    book.completedAt = obj["completedAt"] | static_cast<uint32_t>(0);
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
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    CprVcodexLogs::appendEvent("RST", std::string("Reading stats file empty or unreadable: ") + path);
    return false;
  }
  const bool loaded = loadReadingStats(store, json.c_str());
  if (!loaded) {
    CprVcodexLogs::appendEvent("RST", std::string("Failed to load reading stats from ") + path);
  }
  return loaded;
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
    CprVcodexLogs::appendEvent("ACH", std::string("Achievements JSON parse error: ") + error.c_str());
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  store.dirty = false;
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
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    CprVcodexLogs::appendEvent("ACH", std::string("Achievements file empty or unreadable: ") + path);
    return false;
  }
  const bool loaded = loadAchievements(store, json.c_str());
  if (!loaded) {
    CprVcodexLogs::appendEvent("ACH", std::string("Failed to load achievements from ") + path);
  }
  return loaded;
}
