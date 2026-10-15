#include <gtest/gtest.h>

#include "RecoveryAdapter.h"

class KOReaderSyncRecoveryTest : public testing::Test {
 protected:
  GfxRenderer renderer;
  MappedInputManager input;
  void SetUp() override {
    fakeState = {};
    auto& sync = fakeState.koReaderSyncSession;
    sync.active = true;
    sync.outcome = KOReaderSyncOutcomeState::PENDING;
    sync.epubPath = "/book.epub";
    sync.spineIndex = 4;
    sync.page = 12;
    sync.totalPagesInSpine = 41;
    sync.paragraphIndex = 17;
    sync.hasParagraphIndex = true;
    sync.xhtmlSeekHint = 2198;
    sync.hasLocalKoReaderPosition = true;
    sync.localKoReaderProgress = "/body/DocFragment[5]/body/p[17]/text().23";
    sync.localKoReaderPercentage = 0.42f;
    sync.localChapterLabel = "Chapter five";
    fakeStore = {};
    KOReaderProfile profile;
    profile.username = "saved-user";
    profile.password = "saved-password";
    fakeStore.profiles.push_back(profile);
    restartCalls = 0;
    restartSupported = true;
    KOReaderSyncClient::eligible = true;
  }
  std::unique_ptr<KOReaderSyncActivity> initialActivity() {
    auto activity = KOReaderSyncActivity::resumeAfterMemoryRecovery(renderer, input);
    if (activity) activity->memoryRecoveryAttempted = false;
    return activity;
  }
};

TEST_F(KOReaderSyncRecoveryTest, ResumePreservesPositionAndBlocksAnotherRestart) {
  const auto activity = KOReaderSyncActivity::resumeAfterMemoryRecovery(renderer, input);
  ASSERT_NE(activity, nullptr);
  EXPECT_EQ(activity->epubPath, "/book.epub");
  EXPECT_EQ(activity->currentSpineIndex, 4);
  EXPECT_EQ(activity->currentPage, 12);
  EXPECT_EQ(activity->totalPagesInSpine, 41);
  EXPECT_EQ(activity->paragraphIndex, 17);
  EXPECT_TRUE(activity->hasParagraphIndex);
  EXPECT_EQ(activity->xhtmlSeekHint, 2198);
  EXPECT_EQ(activity->localProgress.xpath, fakeState.koReaderSyncSession.localKoReaderProgress);
  EXPECT_FLOAT_EQ(activity->localProgress.percentage, 0.42f);
  EXPECT_EQ(activity->localChapterLabel, "Chapter five");
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::COMPARE));
  EXPECT_EQ(restartCalls, 0);
}

TEST_F(KOReaderSyncRecoveryTest, SavesComputedProgressBeforeResumingExplicitUpload) {
  const auto activity = initialActivity();
  activity->localProgress = {"/body/p[21]/text().8", 0.53f};
  activity->localChapterLabel = "Updated chapter";
  EXPECT_TRUE(activity->recoverFromLowMemory(KOReaderSyncIntentState::PUSH_LOCAL));
  EXPECT_EQ(fakeState.saveCalls, 1);
  EXPECT_EQ(restartCalls, 1);
  EXPECT_EQ(lastRequest.operation, KOReaderRecoveryOperation::ManualSync);
  EXPECT_EQ(lastRequest.profileIndex, 0);
  EXPECT_EQ(lastRequest.profileIdentity,
            KOReaderRecovery::profileIdentity("saved-user", "https://sync.koreader.rocks:443"));
  EXPECT_EQ(fakeState.koReaderSyncSession.intent, KOReaderSyncIntentState::PUSH_LOCAL);
  EXPECT_EQ(fakeState.koReaderSyncSession.localKoReaderProgress, activity->localProgress.xpath);
  EXPECT_FLOAT_EQ(fakeState.koReaderSyncSession.localKoReaderPercentage, 0.53f);
  EXPECT_EQ(fakeState.koReaderSyncSession.outcome, KOReaderSyncOutcomeState::PENDING);
}

TEST_F(KOReaderSyncRecoveryTest, FailedStateSaveNeverRestarts) {
  const auto activity = initialActivity();
  fakeState.saveSucceeds = false;
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::PUSH_LOCAL));
  EXPECT_EQ(fakeState.koReaderSyncSession.intent, KOReaderSyncIntentState::COMPARE);
  EXPECT_EQ(restartCalls, 0);
}

TEST_F(KOReaderSyncRecoveryTest, TransportFailureNeverRestartsOrSaves) {
  const auto activity = initialActivity();
  KOReaderSyncClient::eligible = false;
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::COMPARE));
  EXPECT_EQ(fakeState.saveCalls, 0);
  EXPECT_EQ(restartCalls, 0);
}

TEST_F(KOReaderSyncRecoveryTest, DifferentBookNeverRestarts) {
  const auto activity = initialActivity();
  fakeState.koReaderSyncSession.epubPath = "/other.epub";
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::COMPARE));
  EXPECT_EQ(restartCalls, 0);
}

TEST_F(KOReaderSyncRecoveryTest, MissingProfileNeverRestarts) {
  const auto activity = initialActivity();
  fakeStore.active = -1;
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::COMPARE));
  EXPECT_EQ(fakeState.saveCalls, 0);
  EXPECT_EQ(restartCalls, 0);
}

TEST_F(KOReaderSyncRecoveryTest, UnsupportedRestartIsAttemptedOnlyOnce) {
  const auto activity = initialActivity();
  restartSupported = false;
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::COMPARE));
  EXPECT_FALSE(activity->recoverFromLowMemory(KOReaderSyncIntentState::COMPARE));
  EXPECT_EQ(restartCalls, 1);
}

TEST_F(KOReaderSyncRecoveryTest, CompletedSessionCannotBeReplayed) {
  fakeState.koReaderSyncSession.outcome = KOReaderSyncOutcomeState::UPLOAD_COMPLETE;
  EXPECT_EQ(KOReaderSyncActivity::resumeAfterMemoryRecovery(renderer, input), nullptr);
}
