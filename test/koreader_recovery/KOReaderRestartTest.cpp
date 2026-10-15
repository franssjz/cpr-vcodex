#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "network/KOReaderRecovery.h"

namespace {
namespace BoardConfig {
enum class Board { XteinkX3, XteinkX4, XteinkX4Pro, Other };
struct {
  Board board = Board::XteinkX4;
} ACTIVE;
bool touch = false;
bool hasTouch() { return touch; }
}  // namespace BoardConfig

struct KOReaderProfile {
  std::string username;
  std::string password;
  std::string serverUrl;
};
struct KOReaderCredentialStore {
  static std::string resolveBaseUrl(const std::string& value) { return value; }
};
struct Store {
  std::vector<KOReaderProfile> profiles;
  int active = 0;
  bool saveSucceeds = true;
  int saveCalls = 0;
  const KOReaderProfile* getProfile(size_t index) const { return index < profiles.size() ? &profiles[index] : nullptr; }
  size_t getCount() const { return profiles.size(); }
  int getActiveIndex() const { return active; }
  bool saveToFile() {
    ++saveCalls;
    return saveSucceeds;
  }
} store;
#define KOREADER_STORE store
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)

bool deepSleepInProgress = false;
KOReaderRecovery::Record koReaderRebootRecord{};
constexpr uint32_t SILENT_REBOOT_TARGET_KOREADER = 5;
uint32_t armedTarget = 0;
void armSilentReboot(uint32_t target) { armedTarget = target; }
int renderer = 0;
constexpr int STR_LOADING_POPUP = 1;
const char* tr(int) { return "Loading"; }
struct {
  void drawPopup(int, const char*) {}
} GUI;
void delay(unsigned) {}
struct {
  unsigned calls = 0;
  void restart() { ++calls; }
} ESP;

#include "KOReaderRestart.inc"

class KOReaderRestart : public testing::Test {
 protected:
  void SetUp() override {
    BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4;
    BoardConfig::touch = false;
    deepSleepInProgress = false;
    koReaderRebootRecord = {};
    armedTarget = 0;
    ESP.calls = 0;
    store = {};
    store.profiles = {{"active", "secret-one", "https://active.example.test"},
                      {"selected", "secret-two", "https://selected.example.test"}};
  }
  KOReaderRecoveryRequest signup() const {
    const auto& profile = store.profiles[1];
    return {KOReaderRecoveryOperation::Register, 1, 0,
            KOReaderRecovery::profileIdentity(profile.username, profile.serverUrl)};
  }
};

TEST_F(KOReaderRestart, SavesSelectedNonactiveProfileBeforeArmingAndRestarting) {
  ASSERT_TRUE(silentRestartToKOReader(signup()));
  EXPECT_EQ(store.saveCalls, 1);
  EXPECT_EQ(store.active, 0);
  EXPECT_EQ(ESP.calls, 1u);
  EXPECT_EQ(armedTarget, SILENT_REBOOT_TARGET_KOREADER);
  KOReaderRecoveryRequest resumed{};
  ASSERT_TRUE(KOReaderRecovery::consume(koReaderRebootRecord, true, resumed));
  EXPECT_EQ(resumed.profileIndex, 1);
  EXPECT_EQ(resumed.operation, KOReaderRecoveryOperation::Register);
}

TEST_F(KOReaderRestart, FailedCredentialSaveNeverArmsOrRestarts) {
  store.saveSucceeds = false;
  EXPECT_FALSE(silentRestartToKOReader(signup()));
  EXPECT_EQ(store.saveCalls, 1);
  EXPECT_EQ(ESP.calls, 0u);
  EXPECT_EQ(armedTarget, 0u);
  EXPECT_EQ(koReaderRebootRecord.magic, 0u);
}

TEST_F(KOReaderRestart, ProfileChangesAndActiveAccountMismatchAbortBeforeSaving) {
  auto request = signup();
  store.profiles[1].serverUrl = "https://changed.example.test";
  EXPECT_FALSE(silentRestartToKOReader(request));
  request = signup();
  request.operation = KOReaderRecoveryOperation::Authenticate;
  EXPECT_FALSE(silentRestartToKOReader(request));
  request.operation = KOReaderRecoveryOperation::ManualSync;
  EXPECT_FALSE(silentRestartToKOReader(request));
  EXPECT_EQ(store.saveCalls, 0);
  EXPECT_EQ(ESP.calls, 0u);
}

TEST_F(KOReaderRestart, MissingCredentialsAbortBeforeSaving) {
  const auto request = signup();
  store.profiles[1].password.clear();
  EXPECT_FALSE(silentRestartToKOReader(request));
  EXPECT_EQ(store.saveCalls, 0);
  EXPECT_EQ(ESP.calls, 0u);
}

TEST_F(KOReaderRestart, TouchOtherBoardsAndSleepDoNotRestartOrPersist) {
  const auto request = signup();
  BoardConfig::touch = true;
  EXPECT_FALSE(silentRestartToKOReader(request));
  BoardConfig::touch = false;
  for (const auto board : {BoardConfig::Board::XteinkX4Pro, BoardConfig::Board::Other}) {
    BoardConfig::ACTIVE.board = board;
    EXPECT_FALSE(silentRestartToKOReader(request));
  }
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4;
  deepSleepInProgress = true;
  EXPECT_FALSE(silentRestartToKOReader(request));
  EXPECT_EQ(store.saveCalls, 0);
  EXPECT_EQ(ESP.calls, 0u);
  EXPECT_EQ(koReaderRebootRecord.magic, 0u);
}

TEST_F(KOReaderRestart, X3UsesTheSameBoundedRecoveryPath) {
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX3;
  EXPECT_TRUE(silentRestartToKOReader(signup()));
  EXPECT_EQ(ESP.calls, 1u);
}
}  // namespace
