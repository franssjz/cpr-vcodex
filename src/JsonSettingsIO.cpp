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
#include "SettingsList.h"
#include "WifiCredentialStore.h"
#include "util/ShortcutRegistry.h"
#include "util/TimeZoneRegistry.h"

namespace {
constexpr uint8_t FONT_SIZE_SCHEMA_VERSION = 2;

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

/**
 * @brief Atomically write JSON to file using temp file + rename pattern
 * Prevents corruption on power loss by ensuring file is complete before rename
 * @param moduleName Module identifier for logging
 * @param path Destination file path
 * @param doc JsonDocument to serialize and write
 * @return true if atomic write succeeded
 * 
 * Pattern: Write to path.tmp → flush() → close() → rename to final path
 * If crash occurs during temp write, original file remains untouched
 * If crash during rename, temp file cleanup on next boot
 */
bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc) {
  // Build temporary file path: "/path/file.json" → "/path/.file.json.tmp"
  char tempPath[256];
  snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

  // Write to temporary file first
  HalFile file;
  if (!Storage.openFileForWrite(moduleName, tempPath, file)) {
    LOG_ERR(moduleName, "Could not open temp JSON file for write: %s", tempPath);
    return false;
  }

  const size_t written = serializeJson(doc, file);
  file.flush();
  const bool closeOk = file.close();
  
  if (written == 0 || !closeOk) {
    LOG_ERR(moduleName, "Failed to write JSON to temp file, removing: %s", tempPath);
    Storage.remove(tempPath);
    return false;
  }

  // Atomic rename: temp file → final path
  // On SPIFFS/SdFat, rename with target existing deletes target first
  if (!Storage.rename(tempPath, path)) {
    LOG_ERR(moduleName, "Failed atomic rename from %s to %s", tempPath, path);
    Storage.remove(tempPath);  // Clean up temp file
    return false;
  }

  LOG_DBG(moduleName, "Atomically wrote JSON to: %s", path);
  return true;
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

/**
 * @brief Safely load JSON document with validation and fixed capacity
 * @param moduleName Module identifier for logging
 * @param path File path to load from
 * @param doc JsonDocument to populate (must have fixed capacity)
 * @param requiredFields Array of required field names for validation
 * @return true if loaded and validated successfully
 * 
 * Uses fixed capacity to prevent dynamic memory allocation and heap fragmentation.
 * Validates required fields to prevent crashes from corrupted JSON files.
 */
bool safeLoadJsonDocument(const char* moduleName, const char* path, JsonDocument& doc, 
                         const std::vector<const char*>& requiredFields = {}) {
  if (!loadJsonDocumentFromFile(moduleName, path, doc)) {
    return false;
  }
  
  // Validate required fields to prevent runtime crashes
  for (const char* field : requiredFields) {
    if (!doc.containsKey(field)) {
      LOG_ERR(moduleName, "Invalid JSON: missing required field '%s'", field);
      return false;
    }
  }
  
  return true;
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
  doc["lastAutoTimeSync"] = s.lastAutoTimeSync;

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
  s.lastAutoTimeSync = doc["lastAutoTimeSync"] | static_cast<uint32_t>(0);
  return true;
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  // Use fixed 16KB capacity to prevent dynamic memory allocation
  // This prevents heap fragmentation when settings are saved frequently
  JsonDocument doc(16384);

  for (const auto& info : getSettingsList()) {
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
  doc["settingsShortcutVisible"] = s.settingsShortcutVisible;
  doc["readingStatsShortcutVisible"] = s.readingStatsShortcutVisible;
  doc["readingHeatmapShortcutVisible"] = s.readingHeatmapShortcutVisible;
  doc["achievementsShortcutVisible"] = s.achievementsShortcutVisible;
  doc["ifFoundShortcutVisible"] = s.ifFoundShortcutVisible;
  doc["readMeShortcutVisible"] = s.readMeShortcutVisible;
  doc["recentBooksShortcutVisible"] = s.recentBooksShortcutVisible;
  doc["bookmarksShortcutVisible"] = s.bookmarksShortcutVisible;
  doc["fileTransferShortcutVisible"] = s.fileTransferShortcutVisible;
  doc["sleepShortcutVisible"] = s.sleepShortcutVisible;
  doc["fontSizeSchemaVersion"] = FONT_SIZE_SCHEMA_VERSION;

  // Check for capacity overflow before writing
  if (doc.overflowed()) {
    LOG_ERR("CPS", "Settings JSON document exceeded 16KB capacity, save aborted");
    return false;
  }

  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  // Use fixed 16KB capacity for consistent memory behavior and to detect oversized payloads
  JsonDocument doc(16384);
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  if (doc.overflowed()) {
    LOG_ERR("CPS", "Settings JSON exceeded 16KB capacity (possible corruption or attack)");
    return false;
  }

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
      // Use fixed 256-byte buffer instead of std::string to avoid heap fragmentation
      // Most settings fields are < 128 bytes; 256 provides 2x headroom
      char valBuf[256];
      const size_t maxLen = sizeof(valBuf) - 1;
      
      // Read current default into fixed buffer (O(1) no allocation)
      strncpy(valBuf, strPtr, maxLen);
      valBuf[maxLen] = '\0';
      const std::string_view fieldDefault(valBuf);  // View only, no allocation

      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        if (needsResave) *needsResave = true;
        continue;
      }

      if (info.obfuscated) {
        // Avoid creating temporary std::string for key lookup
        // Construct obfuscated key name in fixed buffer
        char obfuscatedKey[128];
        snprintf(obfuscatedKey, sizeof(obfuscatedKey), "%s_obf", info.key);
        
        bool ok = false;
        const char* obfuscatedValue = doc[obfuscatedKey] | "";
        std::string deobfuscated = obfuscation::deobfuscateFromBase64(obfuscatedValue, &ok);
        
        // Copy deobfuscated result into fixed buffer
        if (ok && !deobfuscated.empty()) {
          strncpy(valBuf, deobfuscated.c_str(), maxLen);
          valBuf[maxLen] = '\0';
        } else {
          // Fallback to standard key if obfuscated fails
          const char* standardValue = doc[info.key] | "";
          strncpy(valBuf, standardValue, maxLen);
          valBuf[maxLen] = '\0';
          if (strcmp(valBuf, fieldDefault.data()) != 0 && needsResave) {
            *needsResave = true;
          }
        }
      } else {
        // Non-obfuscated: copy from JSON directly into fixed buffer
        const char* jsonValue = doc[info.key] | "";
        strncpy(valBuf, jsonValue, maxLen);
        valBuf[maxLen] = '\0';
      }
      
      // Copy final result to destination
      strncpy(destPtr, valBuf, info.stringMaxLen - 1);
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
  s.readingStatsShortcutVisible =
      clamp(doc["readingStatsShortcutVisible"] | s.readingStatsShortcutVisible, static_cast<uint8_t>(2),
            s.readingStatsShortcutVisible);
  s.readingHeatmapShortcutVisible =
      clamp(doc["readingHeatmapShortcutVisible"] | s.readingHeatmapShortcutVisible, static_cast<uint8_t>(2),
            s.readingHeatmapShortcutVisible);
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
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
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

  store.recentBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

bool JsonSettingsIO::loadRecentBooksFromFile(RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  if (!loadJsonDocumentFromFile("RBS", path, doc)) {
    return false;
  }

  store.recentBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- ReadingStatsStore ----

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;

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

  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = books.add<JsonObject>();
    obj["path"] = book.path;
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
  JsonDocument doc(8192); // Fixed 8KB capacity to prevent dynamic allocation and heap fragmentation
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RST", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();

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

  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject obj : books) {
    ReadingBookStats book;
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
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
    store.books.push_back(std::move(book));
  }

  store.rebuildAggregatedReadingDays();
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore& store, const char* path) {
  JsonDocument doc(8192); // Fixed 8KB capacity for memory efficiency
  std::vector<const char*> requiredFields = {"formatVersion", "books"};
  if (!safeLoadJsonDocument("RST", path, doc, requiredFields)) {
    return false;
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();

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

  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject obj : books) {
    ReadingBookStats book;
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
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
    store.books.push_back(std::move(book));
  }

  store.rebuildAggregatedReadingDays();
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

// ---- AchievementsStore ----

bool JsonSettingsIO::saveAchievements(const AchievementsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 1;
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
    const std::string pathValue = value | std::string("");
    if (!pathValue.empty()) {
      store.startedBooks.push_back(pathValue);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    const std::string pathValue = value | std::string("");
    if (!pathValue.empty()) {
      store.finishedBooks.push_back(pathValue);
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
    const std::string pathValue = value | std::string("");
    if (!pathValue.empty()) {
      store.startedBooks.push_back(pathValue);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    const std::string pathValue = value | std::string("");
    if (!pathValue.empty()) {
      store.finishedBooks.push_back(pathValue);
    }
  }

  return true;
}
