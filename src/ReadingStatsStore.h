#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"

class ReadingStatsStore;

namespace JsonSettingsIO {
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);
}  // namespace JsonSettingsIO

inline uint64_t getDailyReadingGoalMs() { return SETTINGS.getDailyGoalMs(); }

/**
 * @brief Represents reading statistics for a single day
 */
struct ReadingDayStats {
  uint32_t dayOrdinal = 0;      /**< Day ordinal (days since epoch) */
  uint64_t readingMs = 0;       /**< Total reading time in milliseconds for this day */
};

/**
 * @brief Represents reading statistics for a single book
 */
struct ReadingBookStats {
  std::string path;                        /**< File path of the book */
  std::string title;                       /**< Book title */
  std::string author;                      /**< Book author */
  std::string coverBmpPath;                /**< Path to cover image */
  std::string chapterTitle;                /**< Current chapter title */
  std::vector<ReadingDayStats> readingDays; /**< Reading time per day for this book */
  uint64_t totalReadingMs = 0;             /**< Total reading time across all sessions */
  uint32_t sessions = 0;                   /**< Number of reading sessions */
  uint32_t lastSessionMs = 0;              /**< Duration of last session in milliseconds */
  uint32_t firstReadAt = 0;                /**< Timestamp of first read (Unix epoch) */
  uint32_t lastReadAt = 0;                 /**< Timestamp of last read (Unix epoch) */
  uint8_t lastProgressPercent = 0;         /**< Last reading progress percentage (0-100) */
  uint8_t chapterProgressPercent = 0;      /**< Current chapter progress percentage (0-100) */
  bool completed = false;                  /**< Whether the book has been completed */
};

/**
 * @brief Snapshot of a reading session for analytics and achievements
 */
struct ReadingSessionSnapshot {
  bool valid = false;                      /**< Whether this snapshot is valid */
  uint32_t serial = 0;                     /**< Unique session serial number */
  std::string path;                        /**< Book path for this session */
  uint32_t sessionMs = 0;                  /**< Session duration in milliseconds */
  bool counted = false;                    /**< Whether this session counted toward statistics */
  bool completedThisSession = false;       /**< Whether the book was completed in this session */
  uint8_t startProgressPercent = 0;        /**< Progress at session start (0-100) */
  uint8_t endProgressPercent = 0;          /**< Progress at session end (0-100) */
};

/**
 * @brief Singleton class for managing reading statistics and achievements
 *
 * This class tracks reading time, progress, and generates statistics for all books.
 * Data is persisted to SD card with deferred saves to optimize I/O performance.
 * Statistics are used for achievements, heatmaps, and reading analytics.
 */
class ReadingStatsStore {
  static ReadingStatsStore instance;

  struct SummaryCache {
    bool valid = false;
    uint32_t referenceDayOrdinal = 0;
    uint32_t booksFinishedCount = 0;
    uint64_t totalReadingMs = 0;
    uint64_t todayReadingMs = 0;
    uint64_t recent7ReadingMs = 0;
    uint64_t recent30ReadingMs = 0;
    uint32_t currentStreakDays = 0;
    uint32_t maxStreakDays = 0;
    uint64_t goalReadingMs = 0;
  };

  struct SessionState {
    bool active = false;
    size_t bookIndex = 0;
    unsigned long lastInteractionMs = 0;
    uint64_t accumulatedMs = 0;
    uint8_t startProgressPercent = 0;
    bool startCompleted = false;
  };

  std::vector<ReadingBookStats> books;
  std::vector<ReadingDayStats> legacyReadingDays;
  std::vector<ReadingDayStats> readingDays;
  SessionState activeSession;
  ReadingSessionSnapshot lastSessionSnapshot;
  uint32_t sessionSerialCounter = 0;
  mutable SummaryCache summaryCache;
  mutable bool dirty = false;
  mutable unsigned long lastSaveMs = 0;

  friend bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStats(ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore&, const char*);

  size_t getOrCreateBookIndex(const std::string& path, const std::string& title, const std::string& author,
                              const std::string& coverBmpPath);
  void touchBook(size_t index);
  ReadingDayStats& getOrCreateReadingDay(uint32_t epochSeconds);
  ReadingDayStats& getOrCreateBookReadingDay(ReadingBookStats& book, uint32_t epochSeconds);
  uint32_t getLatestKnownTimestamp() const;
  uint32_t getReferenceTimestamp(uint32_t preferredTimestamp, uint32_t bookTimestamp = 0) const;
  uint32_t getReferenceDayOrdinal() const;
  void updateBookReadTimestamp(ReadingBookStats& book, uint32_t preferredTimestamp);
  void recordReadingTime(ReadingBookStats& book, uint32_t epochSeconds, uint64_t readingMs);
  void rebuildAggregatedReadingDays();
  bool removeIgnoredBooks();
  void invalidateSummaryCache();
  void rebuildSummaryCache() const;
  bool shouldSaveDeferred() const;
  void markDirty();
  bool persistToFile(const char* path) const;
  static bool isClockValid(uint32_t epochSeconds);

 public:
  ~ReadingStatsStore() = default;

  /**
   * @brief Get the singleton instance of ReadingStatsStore
   * @return Reference to the singleton instance
   */
  static ReadingStatsStore& getInstance() { return instance; }

  /**
   * @brief Begin tracking a new reading session
   * @param path Book file path
   * @param title Book title
   * @param author Book author
   * @param coverBmpPath Path to cover image
   * @param progressPercent Current reading progress (0-100)
   * @param chapterTitle Current chapter title
   * @param chapterProgressPercent Current chapter progress (0-100)
   */
  void beginSession(const std::string& path, const std::string& title, const std::string& author,
                    const std::string& coverBmpPath, uint8_t progressPercent = 0,
                    const std::string& chapterTitle = "", uint8_t chapterProgressPercent = 0);

  /**
   * @brief Record user activity during reading session
   * Updates accumulated reading time and marks data as dirty for persistence
   */
  void noteActivity();

  /**
   * @brief Periodic tick for active reading session
   * Called regularly to check for activity timeouts and trigger deferred saves
   */
  void tickActiveSession();

  /**
   * @brief Resume a previously paused reading session
   * Resets activity timer to prevent premature session end
   */
  void resumeSession();

  /**
   * @brief Update reading progress for current session
   * @param progressPercent Current progress percentage (0-100)
   * @param completed Whether the book is now completed
   * @param chapterTitle Current chapter title
   * @param chapterProgressPercent Chapter progress percentage (0-100)
   */
  void updateProgress(uint8_t progressPercent, bool completed = false, const std::string& chapterTitle = "",
                      uint8_t chapterProgressPercent = 0);

  /**
   * @brief End the current reading session
   * Saves final statistics and clears session state
   */
  void endSession();

  /**
   * @brief Update metadata for an existing book
   * @param path Book file path
   * @param title New book title
   * @param author New book author
   * @param coverBmpPath New cover image path
   * @return true if book was found and updated
   */
  bool updateBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                          const std::string& coverBmpPath);

  /**
   * @brief Remove a book and its statistics
   * @param path Book file path to remove
   * @return true if book was found and removed
   */
  bool removeBook(const std::string& path);

  /**
   * @brief Find book statistics by path
   * @param path Book file path
   * @return Pointer to book stats, or nullptr if not found
   */
  const ReadingBookStats* findBook(const std::string& path) const;

  /**
   * @brief Get snapshot of the last completed reading session
   * @return Reference to last session snapshot
   */
  const ReadingSessionSnapshot& getLastSessionSnapshot() const { return lastSessionSnapshot; }

  /**
   * @brief Get all book statistics
   * @return Const reference to vector of all books
   */
  const std::vector<ReadingBookStats>& getBooks() const { return books; }

  /**
   * @brief Get daily reading statistics
   * @return Const reference to vector of daily stats
   */
  const std::vector<ReadingDayStats>& getReadingDays() const { return readingDays; }

  /**
   * @brief Check if a file path should be ignored for statistics
   * @param path File path to check
   * @return true if path should be ignored
   */
  static bool shouldIgnorePath(const std::string& path);

  /**
   * @brief Get count of books that have been started
   * @return Number of books with reading sessions
   */
  uint32_t getBooksStartedCount() const { return static_cast<uint32_t>(books.size()); }

  /**
   * @brief Get count of books that have been completed
   * @return Number of completed books
   */
  uint32_t getBooksFinishedCount() const;

  /**
   * @brief Get total reading time across all books and sessions
   * @return Total reading time in milliseconds
   */
  uint64_t getTotalReadingMs() const;

  /**
   * @brief Get reading time for today
   * @return Today's reading time in milliseconds
   */
  uint64_t getTodayReadingMs() const;

  /**
   * @brief Get reading time for recent days
   * @param days Number of recent days to include
   * @return Reading time in milliseconds for the last N days
   */
  uint64_t getRecentReadingMs(uint32_t days) const;

  /**
   * @brief Get current reading streak in days
   * @return Number of consecutive days with reading
   */
  uint32_t getCurrentStreakDays() const;

  /**
   * @brief Get maximum reading streak in days
   * @return Longest streak of consecutive reading days
   */
  uint32_t getMaxStreakDays() const;

  /**
   * @brief Get a representative timestamp for display purposes
   * @param usedFallback Optional pointer to indicate if fallback was used
   * @return Timestamp suitable for display
   */
  uint32_t getDisplayTimestamp(bool* usedFallback = nullptr) const;

  /**
   * @brief Check if any reading day statistics exist
   * @return true if reading days data is available
   */
  bool hasReadingDays() const { return !readingDays.empty(); }

  /**
   * @brief Reset all reading statistics and clear stored data
   */
  void reset();

  /**
   * @brief Export reading statistics to a file
   * @param path File path to export to
   * @return true if export succeeded
   */
  bool exportToFile(const std::string& path) const;

  /**
   * @brief Import reading statistics from a file
   * @param path File path to import from
   * @return true if import succeeded
   */
  bool importFromFile(const std::string& path);

  /**
   * @brief Save reading statistics to the default file
   * @return true if save succeeded
   */
  bool saveToFile() const;

  /**
   * @brief Load reading statistics from the default file
   * @return true if load succeeded
   */
  bool loadFromFile();
};

#define READING_STATS ReadingStatsStore::getInstance()
