#include <gtest/gtest.h>

#include "network/KOReaderRecovery.h"
#include "network/KOReaderSyncRecovery.h"

namespace {
KOReaderRecoveryRequest requestFor(const KOReaderRecoveryOperation operation, const uint8_t index = 2) {
  return {operation, index, 0, KOReaderRecovery::profileIdentity("reader", "https://sync.example.test")};
}
}  // namespace

TEST(KOReaderRecovery, NonactiveRegistrationKeepsItsSavedProfileAndIsConsumedOnce) {
  KOReaderRecovery::Record rtc{};
  const auto original = requestFor(KOReaderRecoveryOperation::Register);
  ASSERT_TRUE(KOReaderRecovery::arm(rtc, original));
  KOReaderRecoveryRequest resumed{};
  ASSERT_TRUE(KOReaderRecovery::consume(rtc, true, resumed));
  EXPECT_EQ(resumed.operation, KOReaderRecoveryOperation::Register);
  EXPECT_EQ(resumed.profileIndex, 2);
  EXPECT_EQ(resumed.profileIdentity, original.profileIdentity);
  EXPECT_TRUE(KOReaderRecovery::matchesProfile(resumed, 3, 0, original.profileIdentity));
  EXPECT_EQ(rtc.magic, 0u);
  EXPECT_EQ(rtc.checksum, 0u);
  EXPECT_FALSE(KOReaderRecovery::consume(rtc, true, resumed));
}

TEST(KOReaderRecovery, AuthenticationAndManualSyncRequireTheSameActiveAccount) {
  for (const auto operation : {KOReaderRecoveryOperation::Authenticate, KOReaderRecoveryOperation::ManualSync}) {
    const auto request = requestFor(operation);
    EXPECT_TRUE(KOReaderRecovery::matchesProfile(request, 3, 2, request.profileIdentity));
    EXPECT_FALSE(KOReaderRecovery::matchesProfile(request, 3, 0, request.profileIdentity));
    EXPECT_FALSE(KOReaderRecovery::matchesProfile(request, 3, -1, request.profileIdentity));
  }
}

TEST(KOReaderRecovery, RemovedReorderedOrEditedProfilesCannotResumeAnotherAccount) {
  const auto request = requestFor(KOReaderRecoveryOperation::Register);
  EXPECT_FALSE(KOReaderRecovery::matchesProfile(request, 2, 0, request.profileIdentity));
  EXPECT_FALSE(KOReaderRecovery::matchesProfile(
      request, 3, 0, KOReaderRecovery::profileIdentity("another-reader", "https://sync.example.test")));
  EXPECT_FALSE(KOReaderRecovery::matchesProfile(
      request, 3, 0, KOReaderRecovery::profileIdentity("reader", "https://other.example.test")));
  // Field lengths distinguish an account/server boundary from concatenation.
  EXPECT_NE(KOReaderRecovery::profileIdentity("ab", "c"), KOReaderRecovery::profileIdentity("a", "bc"));
}

TEST(KOReaderRecovery, ColdBootAndOtherRestartTargetsDiscardPendingWork) {
  KOReaderRecovery::Record rtc{};
  ASSERT_TRUE(KOReaderRecovery::arm(rtc, requestFor(KOReaderRecoveryOperation::Register)));
  KOReaderRecoveryRequest resumed{};
  EXPECT_FALSE(KOReaderRecovery::consume(rtc, false, resumed));
  EXPECT_FALSE(KOReaderRecovery::consume(rtc, true, resumed));
}

TEST(KOReaderRecovery, CorruptOrUnknownRecordsAreClearedWithoutDispatch) {
  KOReaderRecovery::Record valid{};
  ASSERT_TRUE(KOReaderRecovery::arm(valid, requestFor(KOReaderRecoveryOperation::Register)));
  for (size_t byte = 0; byte < sizeof(valid); ++byte) {
    auto rtc = valid;
    reinterpret_cast<unsigned char*>(&rtc)[byte] ^= 1;
    KOReaderRecoveryRequest resumed{};
    EXPECT_FALSE(KOReaderRecovery::consume(rtc, true, resumed)) << byte;
    EXPECT_EQ(rtc.magic, 0u) << byte;
  }
  auto rtc = valid;
  rtc.version++;
  rtc.checksum = KOReaderRecovery::checksum(rtc);
  KOReaderRecoveryRequest resumed{};
  EXPECT_FALSE(KOReaderRecovery::consume(rtc, true, resumed));
}

TEST(KOReaderRecovery, InvalidRequestsCannotLeaveAPreviouslyArmedActionBehind) {
  const auto valid = requestFor(KOReaderRecoveryOperation::Register);
  for (unsigned field = 0; field < 3; ++field) {
    KOReaderRecovery::Record rtc{};
    ASSERT_TRUE(KOReaderRecovery::arm(rtc, valid));
    auto invalid = valid;
    if (field == 0) invalid.profileIndex = 8;
    if (field == 1) invalid.operation = static_cast<KOReaderRecoveryOperation>(255);
    if (field == 2) invalid.reserved = 1;
    EXPECT_FALSE(KOReaderRecovery::arm(rtc, invalid));
    KOReaderRecoveryRequest resumed{};
    EXPECT_FALSE(KOReaderRecovery::consume(rtc, true, resumed));
  }
}

TEST(KOReaderRecovery, OnlyFirstExplicitPreflightRejectionCanRequestRecovery) {
  EXPECT_TRUE(KOReaderRecovery::shouldRecover(false, true));
  EXPECT_FALSE(KOReaderRecovery::shouldRecover(false, false));
  EXPECT_FALSE(KOReaderRecovery::shouldRecover(true, true));
  EXPECT_FALSE(KOReaderRecovery::shouldRecover(true, false));
}

TEST(KOReaderRecovery, PendingManualSyncIntentsCanResumeWithoutChangingPosition) {
  KOReaderSyncSessionState session;
  session.active = true;
  session.outcome = KOReaderSyncOutcomeState::PENDING;
  session.epubPath = "/books/example.epub";
  session.spineIndex = 4;
  session.page = 7;
  session.paragraphIndex = 19;
  session.hasParagraphIndex = true;
  session.localKoReaderProgress = "/body/DocFragment[5]/body/p[20]";
  for (const auto intent :
       {KOReaderSyncIntentState::COMPARE, KOReaderSyncIntentState::PULL_REMOTE, KOReaderSyncIntentState::PUSH_LOCAL}) {
    session.intent = intent;
    EXPECT_TRUE(KOReaderSyncRecovery::canResume(session));
  }
  EXPECT_EQ(session.spineIndex, 4);
  EXPECT_EQ(session.page, 7);
  EXPECT_EQ(session.paragraphIndex, 19);
  EXPECT_TRUE(session.hasParagraphIndex);
  EXPECT_EQ(session.localKoReaderProgress, "/body/DocFragment[5]/body/p[20]");
}

TEST(KOReaderRecovery, IncompleteAutomaticOrFinishedHandoffsCannotDispatchSync) {
  KOReaderSyncSessionState valid;
  valid.active = true;
  valid.outcome = KOReaderSyncOutcomeState::PENDING;
  valid.epubPath = "/books/example.epub";
  ASSERT_TRUE(KOReaderSyncRecovery::canResume(valid));
  auto changed = valid;
  changed.active = false;
  EXPECT_FALSE(KOReaderSyncRecovery::canResume(changed));
  changed = valid;
  changed.epubPath.clear();
  EXPECT_FALSE(KOReaderSyncRecovery::canResume(changed));
  changed = valid;
  changed.exitToHomeAfterSync = true;
  EXPECT_FALSE(KOReaderSyncRecovery::canResume(changed));
  changed = valid;
  changed.autoPullEpubPath = valid.epubPath;
  EXPECT_FALSE(KOReaderSyncRecovery::canResume(changed));
  for (const auto intent : {KOReaderSyncIntentState::AUTO_PULL, KOReaderSyncIntentState::AUTO_PUSH,
                            static_cast<KOReaderSyncIntentState>(255)}) {
    changed = valid;
    changed.intent = intent;
    EXPECT_FALSE(KOReaderSyncRecovery::canResume(changed));
  }
  for (const auto outcome :
       {KOReaderSyncOutcomeState::NONE, KOReaderSyncOutcomeState::CANCELLED, KOReaderSyncOutcomeState::FAILED,
        KOReaderSyncOutcomeState::UPLOAD_COMPLETE, KOReaderSyncOutcomeState::APPLIED_REMOTE}) {
    changed = valid;
    changed.outcome = outcome;
    EXPECT_FALSE(KOReaderSyncRecovery::canResume(changed));
  }
}
