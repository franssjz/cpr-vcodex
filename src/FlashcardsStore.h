#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FlashcardCard {
  std::string key;
  std::string front;
  std::string back;
};

struct FlashcardCardProgress {
  std::string key;
  uint32_t seenCount = 0;
  uint32_t successCount = 0;
  uint32_t failCount = 0;
  uint32_t skipCount = 0;
  uint32_t lastReviewedDay = 0;
  uint32_t dueDay = 0;
  uint16_t intervalDays = 0;
};

struct FlashcardDeck {
  std::string deckId;
  std::string path;
  std::string title;
  std::vector<FlashcardCard> cards;
};

struct FlashcardDeckRecord {
  std::string deckId;
  std::string path;
  std::string title;
  uint32_t lastOpenedAt = 0;
  uint32_t lastReviewedAt = 0;
  uint32_t sessionCount = 0;
  uint32_t totalCards = 0;
  uint32_t seenCards = 0;
  uint32_t masteredCards = 0;
  uint32_t dueCards = 0;
  uint32_t totalReviewed = 0;
  uint32_t totalCorrect = 0;
  uint32_t totalWrong = 0;
  uint32_t totalSkipped = 0;
};

struct FlashcardDeckMetrics {
  int totalCards = 0;
  int seenCards = 0;
  int unseenCards = 0;
  int dueCards = 0;
  int masteredCards = 0;
  int totalReviewed = 0;
  int totalCorrect = 0;
  int totalWrong = 0;
  int totalSkipped = 0;
  int successRatePercent = 0;
  uint32_t lastOpenedAt = 0;
  uint32_t lastReviewedAt = 0;
  uint32_t sessionCount = 0;
};

struct FlashcardSessionSummaryData {
  std::string deckId;
  std::string deckPath;
  std::string deckTitle;
  int reviewed = 0;
  int correct = 0;
  int failed = 0;
  int skipped = 0;
  int newSeen = 0;
  int dueRemaining = 0;
  FlashcardDeckMetrics metrics;
};

class FlashcardsStore {
  static FlashcardsStore instance;

  std::vector<FlashcardDeckRecord> knownDecks;
  std::vector<std::string> recentDeckIds;

  FlashcardDeckRecord* findDeckRecordInternal(const std::string& deckId);
  const FlashcardDeckRecord* findDeckRecordInternal(const std::string& deckId) const;
  std::string getIndexPath() const;
  std::string getStatePath(const std::string& deckId) const;
  uint32_t getReferenceDayOrdinal() const;
  uint32_t getReferenceTimestamp() const;
  static std::string getTitleFromPath(const std::string& path);

 public:
  static constexpr int MAX_RECENT_DECKS = 10;

  ~FlashcardsStore() = default;

  static FlashcardsStore& getInstance() { return instance; }

  const std::vector<FlashcardDeckRecord>& getKnownDecks() const { return knownDecks; }
  int getKnownDeckCount() const { return static_cast<int>(knownDecks.size()); }

  std::vector<FlashcardDeckRecord> getRecentDecks() const;
  bool removeRecentDeck(const std::string& deckIdOrPath);
  bool resetDeckStats(const std::string& deckIdOrPath);

  bool saveToFile() const;
  bool loadFromFile();

  bool loadDeck(const std::string& path, FlashcardDeck& deck, std::string* error = nullptr) const;
  bool loadDeckProgress(const FlashcardDeck& deck, std::vector<FlashcardCardProgress>& progress) const;
  bool saveDeckProgress(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress);

  FlashcardDeckMetrics buildMetrics(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress) const;
  std::vector<int> buildSessionQueue(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress) const;

  void registerDeckOpened(const FlashcardDeck& deck, const FlashcardDeckMetrics& metrics);
  void registerSession(const FlashcardDeck& deck, const FlashcardDeckMetrics& metrics);

  const FlashcardDeckRecord* findDeckRecord(const std::string& deckIdOrPath) const;

  void markCardSuccess(FlashcardCardProgress& progress) const;
  void markCardFailure(FlashcardCardProgress& progress) const;
  void markCardSkipped(FlashcardCardProgress& progress) const;
};

#define FLASHCARDS FlashcardsStore::getInstance()
