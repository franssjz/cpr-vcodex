#include <HalStorage.h>
#include <MemoryBudget.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <iterator>

#include "FlashcardReviewSession.h"

namespace {
#define LOG_ERR(...) ((void)0)
#define CPR_VCODEX_LOG_EVENT(...) ((void)0)
struct JsonDocument {
  std::string content = "{\"cards\":[]}";
  bool allocationFailed = false;
  bool overflowed() const { return allocationFailed; }
};
size_t measureJson(const JsonDocument& doc) { return doc.content.size(); }
size_t serializeJson(const JsonDocument& doc, HalFile& file) {
  return file.write(doc.content.data(), doc.content.size());
}
#include "SaveFlashcardJson.inc"

constexpr char PATH[] = "/.crosspoint/flashcard_resume.bin";
class FlashcardResume : public testing::Test {
 protected:
  FlashcardDeck deck{"deck-id", "/cards/example.csv", "Example", {{"a", "", ""}, {"b", "", ""}, {"c", "", ""}}};
  std::vector<FlashcardCardProgress> progress{{"a", 1, 1}, {"b"}, {"c"}};
  FlashcardReviewSession original;

  void SetUp() override {
    Storage.root = std::filesystem::temp_directory_path() /
                   ("cpr-flashcards-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    testWriteBudget = std::numeric_limits<size_t>::max();
    testRenameFailure = false;
    testCloseFailure = false;
    MemoryBudget::heap = {256 * 1024, 128 * 1024};
    original.queue = {0, 2, 1, 0};  // completed prefix, shuffled remainder, retry
    original.queueIndex = 1;
    original.initialSessionSize = 3;
    original.showBack = true;
    original.reviewed = 1;
    original.correct = 1;
    original.newSeen = 1;
    original.scrollLineOffset = 4;
  }
  void TearDown() override { std::filesystem::remove_all(Storage.root); }
  bool save() { return original.save(deck, progress, 2, 0); }
  bool load(FlashcardReviewSession& result) { return result.load(deck, progress, 2, 0); }
  std::string bytes() {
    std::ifstream file(Storage.resolve(PATH), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
  void replace(const std::string& data) {
    std::ofstream file(Storage.resolve(PATH), std::ios::binary | std::ios::trunc);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
};

TEST_F(FlashcardResume, RoundTripKeepsCurrentCardSideQueueCountersAndScroll) {
  ASSERT_TRUE(save());
  FlashcardReviewSession resumed;
  ASSERT_TRUE(load(resumed));
  EXPECT_EQ(resumed.queue, (std::vector<int>{2, 1, 0}));
  EXPECT_EQ(resumed.queueIndex, 0u);
  EXPECT_EQ(resumed.initialSessionSize, 3);
  EXPECT_TRUE(resumed.showBack);
  EXPECT_EQ(resumed.reviewed, 1);
  EXPECT_EQ(resumed.correct, 1);
  EXPECT_EQ(resumed.failed, 0);
  EXPECT_EQ(resumed.newSeen, 1);
  EXPECT_EQ(resumed.scrollLineOffset, 4);
  EXPECT_EQ(bytes().size(), 48u + 3u * 2u);
  // Sleeping again must not replay the already completed card.
  ASSERT_TRUE(resumed.save(deck, progress, 2, 0));
  FlashcardReviewSession again;
  ASSERT_TRUE(load(again));
  EXPECT_EQ(again.queue, resumed.queue);
}

TEST_F(FlashcardResume, MissingTruncatedExtendedAndCorruptSnapshotsLeaveSessionUntouched) {
  FlashcardReviewSession result;
  result.queue = {1};
  EXPECT_FALSE(load(result));
  ASSERT_TRUE(save());
  const std::string good = bytes();
  for (size_t length = 0; length < good.size(); ++length) {
    replace(good.substr(0, length));
    EXPECT_FALSE(load(result)) << length;
    EXPECT_EQ(result.queue, (std::vector<int>{1}));
  }
  replace(good + "x");
  EXPECT_FALSE(load(result));
  for (size_t offset = 0; offset < good.size(); ++offset) {
    std::string damaged = good;
    damaged[offset] ^= 0x40;
    replace(damaged);
    EXPECT_FALSE(load(result)) << offset;
    EXPECT_EQ(result.queue, (std::vector<int>{1}));
  }
}

TEST_F(FlashcardResume, ChangedDeckOrderPathRatingsAndSettingsCannotRestoreStaleIndices) {
  ASSERT_TRUE(save());
  FlashcardReviewSession result;
  std::swap(deck.cards[0], deck.cards[1]);
  EXPECT_FALSE(load(result));
  std::swap(deck.cards[0], deck.cards[1]);
  deck.path += ".moved";
  EXPECT_FALSE(load(result));
  deck.path = "/cards/example.csv";
  progress[0].successCount++;
  EXPECT_FALSE(load(result));
  progress[0].successCount--;
  EXPECT_FALSE(result.load(deck, progress, 3, 0));
  EXPECT_FALSE(result.load(deck, progress, 2, 1));
  EXPECT_TRUE(load(result));
}

TEST_F(FlashcardResume, InvalidQueueIndicesCountsAndOverflowAreRejected) {
  original.queue[1] = -1;
  EXPECT_FALSE(save());
  original.queue[1] = 3;
  EXPECT_FALSE(save());
  original.queue[1] = 2;
  original.queueIndex = original.queue.size();
  EXPECT_FALSE(save());
  original.queueIndex = 0;
  original.queue.resize(601, 0);
  EXPECT_FALSE(save());
  original.queue.resize(3);
  original.correct = INT32_MAX;
  original.failed = 1;
  EXPECT_FALSE(save());
}

TEST_F(FlashcardResume, LargestQueueIsBoundedAndLongCompletedPrefixIsNotSaved) {
  deck.cards.resize(600, {"x", "", ""});
  progress.resize(600, {"x"});
  original.queue.assign(5000, 599);
  original.queueIndex = 4400;
  original.initialSessionSize = 600;
  ASSERT_TRUE(save());
  EXPECT_EQ(bytes().size(), 1248u);
  FlashcardReviewSession result;
  ASSERT_TRUE(load(result));
  EXPECT_EQ(result.queue.size(), 600u);
}

TEST_F(FlashcardResume, ShortWriteAndRenameFailureCannotReportSavedSession) {
  ASSERT_TRUE(save());
  const std::string previous = bytes();
  testWriteBudget = 50;
  EXPECT_FALSE(save());
  EXPECT_EQ(bytes(), previous);
  testWriteBudget = std::numeric_limits<size_t>::max();
  testRenameFailure = true;
  EXPECT_FALSE(save());
  EXPECT_FALSE(Storage.exists("/.crosspoint/flashcard_resume.bin.tmp"));
}

TEST_F(FlashcardResume, LowMemoryDoesNotAllocateOrReplaceSession) {
  ASSERT_TRUE(save());
  MemoryBudget::heap = {1000, 1000};
  FlashcardReviewSession result;
  result.queue = {1};
  EXPECT_FALSE(load(result));
  EXPECT_EQ(result.queue, (std::vector<int>{1}));
}

TEST_F(FlashcardResume, RatingsWriterPreservesPreviousFileOnAllocationShortWriteAndCloseFailure) {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  ASSERT_TRUE(saveJsonDocumentToFile("FCS", PATH, doc));
  const std::string previous = bytes();
  doc.content = "{\"cards\":[{\"seenCount\":1}]}";
  doc.allocationFailed = true;
  EXPECT_FALSE(saveJsonDocumentToFile("FCS", PATH, doc));
  EXPECT_EQ(bytes(), previous);
  doc.allocationFailed = false;
  testWriteBudget = 8;
  EXPECT_FALSE(saveJsonDocumentToFile("FCS", PATH, doc));
  EXPECT_EQ(bytes(), previous);
  testWriteBudget = std::numeric_limits<size_t>::max();
  testCloseFailure = true;
  EXPECT_FALSE(saveJsonDocumentToFile("FCS", PATH, doc));
  EXPECT_EQ(bytes(), previous);
  testCloseFailure = false;
  EXPECT_TRUE(saveJsonDocumentToFile("FCS", PATH, doc));
  EXPECT_EQ(bytes(), doc.content);
}
}  // namespace
