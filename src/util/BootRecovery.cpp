#include "BootRecovery.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>

#include <string>

#include "CprVcodexLogs.h"

namespace {
constexpr char RECOVERY_FILE[] = "/.crosspoint/cpr-vcodex-logs/recovery.json";

enum RecoveryBits : uint32_t {
  SKIP_SETTINGS = 1u << 0,
  SKIP_LANGUAGE = 1u << 1,
  SKIP_KOREADER = 1u << 2,
  SKIP_STATE = 1u << 3,
  SKIP_READING_STATS = 1u << 4,
  SKIP_RECENT_BOOKS = 1u << 5,
  SKIP_FAVORITES = 1u << 6,
  SKIP_FLASHCARDS = 1u << 7,
  SKIP_ACHIEVEMENTS = 1u << 8,
  FORCE_HOME = 1u << 9,
  SKIP_OPDS = 1u << 10,
};

RTC_NOINIT_ATTR uint8_t recordedStageRaw = static_cast<uint8_t>(BootRecovery::BootStage::None);
uint32_t recoveryMask = 0;
bool recoveryActive = false;

bool isValidStage(const uint8_t raw) {
  return raw <= static_cast<uint8_t>(BootRecovery::BootStage::Completed);
}

uint32_t getSkipMaskForStage(const BootRecovery::BootStage stage) {
  using Stage = BootRecovery::BootStage;
  switch (stage) {
    case Stage::Settings:
      return SKIP_SETTINGS | FORCE_HOME;
    case Stage::Language:
      return SKIP_LANGUAGE | FORCE_HOME;
    case Stage::KOReader:
      return SKIP_KOREADER | FORCE_HOME;
    case Stage::OPDS:
      return SKIP_OPDS | FORCE_HOME;
    case Stage::UiTheme:
      return SKIP_SETTINGS | SKIP_LANGUAGE | FORCE_HOME;
    case Stage::DisplayAndFonts:
      return SKIP_SETTINGS | SKIP_LANGUAGE | FORCE_HOME;
    case Stage::State:
      return SKIP_STATE | FORCE_HOME;
    case Stage::ReadingStats:
      return SKIP_READING_STATS | SKIP_ACHIEVEMENTS | FORCE_HOME;
    case Stage::RecentBooks:
      return SKIP_RECENT_BOOKS | FORCE_HOME;
    case Stage::Favorites:
      return SKIP_FAVORITES | FORCE_HOME;
    case Stage::Flashcards:
      return SKIP_FLASHCARDS | FORCE_HOME;
    case Stage::Achievements:
      return SKIP_ACHIEVEMENTS | FORCE_HOME;
    case Stage::RouteDecision:
      return FORCE_HOME;
    case Stage::None:
    case Stage::Completed:
    default:
      return 0;
  }
}

void saveRecoveryState() {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CPR_VCODEX_LOG_DIR);

  JsonDocument doc;
  doc["skipMask"] = recoveryMask;
  doc["culpritStage"] = recordedStageRaw;

  String json;
  serializeJson(doc, json);
  const std::string tempPath = std::string(RECOVERY_FILE) + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }
  if (!Storage.writeFile(tempPath.c_str(), json)) {
    CPR_VCODEX_LOG_EVENT("BOOT", "Failed to write recovery temp file");
    return;
  }
  if (Storage.exists(RECOVERY_FILE) && !Storage.remove(RECOVERY_FILE)) {
    Storage.remove(tempPath.c_str());
    CPR_VCODEX_LOG_EVENT("BOOT", "Failed to replace recovery.json");
    return;
  }
  if (!Storage.rename(tempPath.c_str(), RECOVERY_FILE)) {
    Storage.remove(tempPath.c_str());
    CPR_VCODEX_LOG_EVENT("BOOT", "Failed to finalize recovery.json");
  }
}

void loadRecoveryState() {
  recoveryMask = 0;
  if (!Storage.exists(RECOVERY_FILE)) {
    return;
  }

  const String json = Storage.readFile(RECOVERY_FILE);
  if (json.isEmpty()) {
    return;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    CPR_VCODEX_LOG_EVENT("BOOT", std::string("Failed to parse recovery.json: ") + error.c_str());
    return;
  }

  recoveryMask = doc["skipMask"] | static_cast<uint32_t>(0);
}

void persistPanicInfo() {
#ifndef CPR_DISABLE_EVENT_LOGS
  if (!HalSystem::isRebootFromPanic()) {
    return;
  }

  std::string reportBody = "Recorded boot stage: ";
  reportBody += BootRecovery::getStageName(BootRecovery::getRecordedStage());
  reportBody += "\n\n";
  reportBody += HalSystem::getPanicInfo(true);
  std::string outPath;
  if (CPR_VCODEX_WRITE_REPORT("panic", reportBody, &outPath)) {
    CPR_VCODEX_LOG_EVENT("BOOT", std::string("Saved panic report to ") + outPath);
  }
#endif
}

bool hasMask(const uint32_t bit) { return (recoveryMask & bit) != 0; }

void clearRecoveryState() {
  recoveryMask = 0;
  recoveryActive = false;
  if (Storage.exists(RECOVERY_FILE)) {
    Storage.remove(RECOVERY_FILE);
  }
}
}  // namespace

namespace BootRecovery {

void initialize() {
  if (!isValidStage(recordedStageRaw)) {
    recordedStageRaw = static_cast<uint8_t>(BootStage::None);
  }

  loadRecoveryState();
  persistPanicInfo();

  if (HalSystem::isRebootFromPanic()) {
    const BootStage culpritStage = getRecordedStage();
    const uint32_t stageMask = getSkipMaskForStage(culpritStage);
    if (stageMask != 0) {
      recoveryMask |= stageMask;
      recoveryActive = true;
      saveRecoveryState();

      std::string message = "Panic detected during boot stage ";
      message += getStageName(culpritStage);
      message += "; enabling recovery mode";
      CPR_VCODEX_LOG_EVENT("BOOT", message);
    } else {
      recoveryActive = recoveryMask != 0;
    }
  } else {
    recoveryActive = recoveryMask != 0;
  }

  recordedStageRaw = static_cast<uint8_t>(BootStage::None);
}

void enterStage(const BootStage stage) { recordedStageRaw = static_cast<uint8_t>(stage); }

void markBootCompleted() {
  recordedStageRaw = static_cast<uint8_t>(BootStage::Completed);
  if (recoveryMask != 0 || recoveryActive) {
    CPR_VCODEX_LOG_EVENT("BOOT", "Boot completed successfully; clearing recovery state");
    clearRecoveryState();
  }
}

BootStage getRecordedStage() {
  if (!isValidStage(recordedStageRaw)) {
    return BootStage::None;
  }
  return static_cast<BootStage>(recordedStageRaw);
}

const char* getStageName(const BootStage stage) {
  switch (stage) {
    case BootStage::None:
      return "none";
    case BootStage::Settings:
      return "settings";
    case BootStage::Language:
      return "language";
    case BootStage::KOReader:
      return "koreader";
    case BootStage::OPDS:
      return "opds";
    case BootStage::UiTheme:
      return "uiTheme";
    case BootStage::DisplayAndFonts:
      return "displayAndFonts";
    case BootStage::State:
      return "state";
    case BootStage::ReadingStats:
      return "readingStats";
    case BootStage::RecentBooks:
      return "recentBooks";
    case BootStage::Favorites:
      return "favorites";
    case BootStage::Flashcards:
      return "flashcards";
    case BootStage::Achievements:
      return "achievements";
    case BootStage::RouteDecision:
      return "routeDecision";
    case BootStage::Completed:
      return "completed";
    default:
      return "unknown";
  }
}

bool isRecoveryActive() { return recoveryActive || recoveryMask != 0; }
bool shouldForceHome() { return hasMask(FORCE_HOME); }
bool shouldSkipSettings() { return hasMask(SKIP_SETTINGS); }
bool shouldSkipLanguage() { return hasMask(SKIP_LANGUAGE); }
bool shouldSkipKOReader() { return hasMask(SKIP_KOREADER); }
bool shouldSkipOPDS() { return hasMask(SKIP_OPDS); }
bool shouldSkipState() { return hasMask(SKIP_STATE); }
bool shouldSkipReadingStats() { return hasMask(SKIP_READING_STATS); }
bool shouldSkipRecentBooks() { return hasMask(SKIP_RECENT_BOOKS); }
bool shouldSkipFavorites() { return hasMask(SKIP_FAVORITES); }
bool shouldSkipFlashcards() { return hasMask(SKIP_FLASHCARDS); }
bool shouldSkipAchievements() { return hasMask(SKIP_ACHIEVEMENTS); }

}  // namespace BootRecovery
