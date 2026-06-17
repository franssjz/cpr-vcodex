#pragma once

#include <I18nKeys.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"

class AchievementsStore;
namespace JsonSettingsIO {
bool saveAchievements(const AchievementsStore& store, const char* path);
bool loadAchievements(AchievementsStore& store, const char* json);
bool loadAchievementsFromFile(AchievementsStore& store, const char* path);
}  // namespace JsonSettingsIO

enum class AchievementMetric : uint8_t {
  BooksStarted = 0,
  BooksFinished,
  Sessions,
  TotalReadingMs,
  GoalDays,
  MaxGoalStreak,
  TotalBookmarksAdded,
  MaxSessionMs,
};

enum class AchievementId : uint8_t {
  FirstBookStarted = 0,
  FiveBooksStarted,
  TenBooksStarted,
  TwentyFiveBooksStarted,
  FiftyBooksStarted,
  FirstSession,
  TenSessions,
  TwentyFiveSessions,
  FiftySessions,
  OneHundredSessions,
  TwoHundredSessions,
  FirstBookFinished,
  ThreeBooksFinished,
  FiveBooksFinished,
  TenBooksFinished,
  TwentyBooksFinished,
  ReadingOneHour,
  ReadingFiveHours,
  ReadingTenHours,
  ReadingOneDay,
  ReadingFiftyHours,
  ReadingOneHundredHours,
  ReadingTwoHundredHours,
  FirstGoalDay,
  SevenGoalDays,
  ThirtyGoalDays,
  SixtyGoalDays,
  EightyGoalDays,
  ThreeGoalStreak,
  SevenGoalStreak,
  FourteenGoalStreak,
  ThirtyGoalStreak,
  SixtyGoalStreak,
  FirstBookmark,
  TenBookmarks,
  TwentyFiveBookmarks,
  FiftyBookmarks,
  FifteenMinuteSession,
  ThirtyMinuteSession,
  FortyFiveMinuteSession,
  SixtyMinuteSession,
  NinetyMinuteSession,
  TwoHourSession,
  TwoBooksFinished,
  SevenBooksFinished,
  FifteenBooksFinished,
  TwentyFiveBooksFinished,
  ThirtyBooksFinished,
  FortyBooksFinished,
  FiftyBooksFinished,
  SixtyBooksFinished,
  SeventyFiveBooksFinished,
  OneHundredBooksFinished,
  ThirtyFiveBooksFinished,
  FortyFiveBooksFinished,
  FiftyFiveBooksFinished,
  SixtyFiveBooksFinished,
  SeventyBooksFinished,
  EightyBooksFinished,
  EightyFiveBooksFinished,
  NinetyBooksFinished,
  NinetyFiveBooksFinished,
  _COUNT,
};

struct AchievementDefinition {
  AchievementId id;
  AchievementMetric metric;
  uint64_t target;
  StrId titleId;
};

struct AchievementState {
  bool unlocked = false;
  uint32_t unlockedAt = 0;
};

struct AchievementView {
  const AchievementDefinition* definition = nullptr;
  AchievementState state;
  uint64_t progress = 0;
  uint64_t target = 0;
};

class AchievementsStore {
  static AchievementsStore instance;

  struct ProgressSnapshot {
    uint32_t booksStarted = 0;
    uint32_t booksFinished = 0;
    uint32_t sessions = 0;
    uint64_t totalReadingMs = 0;
    uint32_t goalDays = 0;
    uint32_t maxGoalStreak = 0;
    uint32_t totalBookmarksAdded = 0;
    uint32_t maxSessionMs = 0;
  };

  std::array<AchievementState, static_cast<size_t>(AchievementId::_COUNT)> states = {};
  std::vector<std::string> startedBooks;
  std::vector<std::string> finishedBooks;
  std::vector<AchievementId> pendingUnlocks;
  uint64_t accumulatedReadingMs = 0;
  uint32_t countedSessions = 0;
  uint32_t totalBookmarksAdded = 0;
  uint32_t longestSessionMs = 0;
  uint32_t goalDaysCount = 0;
  uint32_t currentGoalStreak = 0;
  uint32_t maxGoalStreak = 0;
  uint32_t lastGoalDayOrdinal = 0;
  uint32_t resetDayOrdinal = 0;
  uint64_t resetDayBaselineMs = 0;
  uint32_t lastProcessedSessionSerial = 0;
  mutable bool dirty = false;

  friend bool JsonSettingsIO::saveAchievements(const AchievementsStore&, const char*);
  friend bool JsonSettingsIO::loadAchievements(AchievementsStore&, const char*);
  friend bool JsonSettingsIO::loadAchievementsFromFile(AchievementsStore&, const char*);

  static const std::array<AchievementDefinition, static_cast<size_t>(AchievementId::_COUNT)>& definitions();
  static size_t indexOf(AchievementId id) { return static_cast<size_t>(id); }

  static uint32_t getReferenceTimestamp();
  static bool hasString(const std::vector<std::string>& values, const std::string& value);

  void markDirty();
  void unlock(AchievementId id, uint32_t timestamp, bool enqueuePopup);
  ProgressSnapshot buildProgressSnapshot() const;
  void evaluateProgress(bool enqueuePopups);
  void bootstrapFromCurrentStats();
  bool refreshGoalDerivedProgressFromStats();
  uint64_t getEffectiveTodayReadingMs(uint32_t dayOrdinal) const;

 public:
  ~AchievementsStore() = default;

  static AchievementsStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();
  void reset();
  void syncWithPreviousStats();
  void rebuildProgressFromCurrentStats();

  void reconcileFromCurrentStats();
  void recordSessionEnded(const ReadingSessionSnapshot& snapshot);
  void recordBookmarkAdded();

  std::vector<AchievementView> buildViews() const;
  bool hasPendingUnlocks() const { return !pendingUnlocks.empty(); }
  void clearPendingUnlocks() { pendingUnlocks.clear(); }
  std::string popNextPopupMessage();

  const AchievementState& getState(AchievementId id) const { return states[indexOf(id)]; }
  const AchievementDefinition& getDefinition(AchievementId id) const;
  std::string getTitle(AchievementId id) const;
  std::string getDescription(AchievementId id) const;
};

#define ACHIEVEMENTS AchievementsStore::getInstance()
