
#include "ReadingStatsStore.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <algorithm>
#include <ctime>

#include "CrossPointState.h"
#include "util/TimeUtils.h"

namespace {
constexpr char READING_STATS_FILE_JSON[] = "/.crosspoint/reading_stats.json";
constexpr unsigned long MAX_READING_GAP_MS = 30UL * 60UL * 1000UL;
constexpr unsigned long SESSION_HEARTBEAT_MS = 60UL * 1000UL;
constexpr unsigned long DEFERRED_SAVE_INTERVAL_MS = 60UL * 1000UL;
constexpr uint64_t MIN_SESSION_READING_MS = 3ULL * 60ULL * 1000ULL;

uint8_t clampPercent(const uint8_t percent) { return std::min<uint8_t>(percent, 100); }

bool countsForStreak(const ReadingDayStats& day) { return day.readingMs >= getDailyReadingGoalMs(); }

bool isIgnoredStatsPath(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  std::string normalized = FsHelpers::normalisePath(path);
  if (normalized.empty()) {
    return false;
  }

  if (normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }

  return normalized == "/ignore_stats" || normalized.rfind("/ignore_stats/", 0) == 0;
}

void normalizeReadingDays(std::vector<ReadingDayStats>& readingDays) {
  std::sort(readingDays.begin(), readingDays.end(),
            [](const ReadingDayStats& left, const ReadingDayStats& right) {
              return left.dayOrdinal < right.dayOrdinal;
            });

  std::vector<ReadingDayStats> mergedDays;
  mergedDays.reserve(readingDays.size());
  for (const auto& day : readingDays) {
    if (!mergedDays.empty() && mergedDays.back().dayOrdinal == day.dayOrdinal) {
      mergedDays.back().readingMs += day.readingMs;
    } else {
      mergedDays.push_back(day);
    }
  }

  readingDays = std::move(mergedDays);
}

void addReadingToDays(std::vector<ReadingDayStats>& days, const uint32_t dayOrdinal, const uint64_t readingMs) {
  if (dayOrdinal == 0 || readingMs == 0) {
    return;
  }

  auto it = std::lower_bound(days.begin(), days.end(), dayOrdinal,
                             [](const ReadingDayStats& day, const uint32_t ordinal) {
                               return day.dayOrdinal < ordinal;
                             });
  if (it == days.end() || it->dayOrdinal != dayOrdinal) {
    days.insert(it, ReadingDayStats{dayOrdinal, readingMs});
  } else {
    it->readingMs += readingMs;
  }
}
}  // namespace

ReadingStatsStore ReadingStatsStore::instance;

size_t ReadingStatsStore::getOrCreateBookIndex(const std::string& path, const std::string& title,
                                               const std::string& author, const std::string& coverBmpPath) {
  auto it = std::find_if(books.begin(), books.end(), [&](const ReadingBookStats& book) { return book.path == path; });
  if (it == books.end()) {
    books.insert(books.begin(), ReadingBookStats{path, title, author, coverBmpPath});
    return 0;
  }

  if (!title.empty()) {
    it->title = title;
  }
  if (!author.empty()) {
    it->author = author;
  }
  if (!coverBmpPath.empty()) {
    it->coverBmpPath = coverBmpPath;
  }
  return static_cast<size_t>(std::distance(books.begin(), it));
}

const ReadingBookStats* ReadingStatsStore::findBook(const std::string& path) const {
  if (shouldIgnorePath(path)) {
    return nullptr;
  }
  auto it = std::find_if(books.begin(), books.end(), [&](const ReadingBookStats& book) { return book.path == path; });
  return it == books.end() ? nullptr : &(*it);
}

ReadingDayStats& ReadingStatsStore::getOrCreateReadingDay(const uint32_t epochSeconds) {
  const uint32_t dayOrdinal = TimeUtils::getLocalDayOrdinal(epochSeconds);
  auto it = std::lower_bound(readingDays.begin(), readingDays.end(), dayOrdinal,
                             [](const ReadingDayStats& day, const uint32_t ordinal) {
                               return day.dayOrdinal < ordinal;
                             });
  if (it == readingDays.end() || it->dayOrdinal != dayOrdinal) {
    it = readingDays.insert(it, ReadingDayStats{dayOrdinal, 0});
  }
  return *it;
}

ReadingDayStats& ReadingStatsStore::getOrCreateBookReadingDay(ReadingBookStats& book, const uint32_t epochSeconds) {
  const uint32_t dayOrdinal = TimeUtils::getLocalDayOrdinal(epochSeconds);
  auto it = std::lower_bound(book.readingDays.begin(), book.readingDays.end(), dayOrdinal,
                             [](const ReadingDayStats& day, const uint32_t ordinal) {
                               return day.dayOrdinal < ordinal;
                             });
  if (it == book.readingDays.end() || it->dayOrdinal != dayOrdinal) {
    it = book.readingDays.insert(it, ReadingDayStats{dayOrdinal, 0});
  }
  return *it;
}

uint32_t ReadingStatsStore::getLatestKnownTimestamp() const {
  uint32_t latestTimestamp = APP_STATE.lastKnownValidTimestamp;
  for (const auto& book : books) {
    if (isClockValid(book.lastReadAt)) {
      latestTimestamp = std::max(latestTimestamp, book.lastReadAt);
    }
    if (isClockValid(book.firstReadAt)) {
      latestTimestamp = std::max(latestTimestamp, book.firstReadAt);
    }
  }
  return latestTimestamp;
}

uint32_t ReadingStatsStore::getReferenceTimestamp(const uint32_t preferredTimestamp, const uint32_t bookTimestamp) const {
  if (isClockValid(preferredTimestamp)) {
    return preferredTimestamp;
  }

  const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
  if (isClockValid(latestKnownTimestamp)) {
    return latestKnownTimestamp;
  }

  return isClockValid(bookTimestamp) ? bookTimestamp : 0;
}

uint32_t ReadingStatsStore::getReferenceDayOrdinal() const {
  const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp());
  if (isClockValid(referenceTimestamp)) {
    return TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  }
  if (!readingDays.empty()) {
    return readingDays.back().dayOrdinal;
  }
  return 0;
}

void ReadingStatsStore::updateBookReadTimestamp(ReadingBookStats& book, const uint32_t preferredTimestamp) {
  const uint32_t referenceTimestamp = getReferenceTimestamp(preferredTimestamp, book.lastReadAt);
  if (!isClockValid(referenceTimestamp)) {
    return;
  }

  if (book.firstReadAt == 0) {
    book.firstReadAt = referenceTimestamp;
  }
  book.lastReadAt = referenceTimestamp;
}

void ReadingStatsStore::touchBook(const size_t index) {
  if (index == 0 || index >= books.size()) {
    return;
  }

  ReadingBookStats book = books[index];
  books.erase(books.begin() + static_cast<std::ptrdiff_t>(index));
  books.insert(books.begin(), std::move(book));

  if (activeSession.active) {
    if (activeSession.bookIndex == index) {
      activeSession.bookIndex = 0;
    } else if (activeSession.bookIndex < index) {
      activeSession.bookIndex++;
    }
  }
}

bool ReadingStatsStore::isClockValid(const uint32_t epochSeconds) { return TimeUtils::isClockValid(epochSeconds); }

bool ReadingStatsStore::shouldIgnorePath(const std::string& path) { return isIgnoredStatsPath(path); }

void ReadingStatsStore::recordReadingTime(ReadingBookStats& book, const uint32_t epochSeconds, const uint64_t readingMs) {
  if (!isClockValid(epochSeconds) || readingMs == 0) {
    return;
  }

  getOrCreateBookReadingDay(book, epochSeconds).readingMs += readingMs;
  getOrCreateReadingDay(epochSeconds).readingMs += readingMs;
}

void ReadingStatsStore::rebuildAggregatedReadingDays() {
  readingDays = legacyReadingDays;
  normalizeReadingDays(readingDays);

  for (const auto& book : books) {
    for (const auto& day : book.readingDays) {
      addReadingToDays(readingDays, day.dayOrdinal, day.readingMs);
    }
  }
}

bool ReadingStatsStore::removeIgnoredBooks() {
  const size_t originalCount = books.size();
  books.erase(std::remove_if(books.begin(), books.end(),
                             [](const ReadingBookStats& book) {
                               return shouldIgnorePath(book.path);
                             }),
              books.end());
  return books.size() != originalCount;
}

void ReadingStatsStore::invalidateSummaryCache() { summaryCache.valid = false; }

void ReadingStatsStore::markDirty() {
  dirty = true;
  invalidateSummaryCache();
}

bool ReadingStatsStore::shouldSaveDeferred() const {
  if (!dirty) {
    return false;
  }
  if (!activeSession.active) {
    return true;
  }
  return lastSaveMs == 0 || (millis() - lastSaveMs) >= DEFERRED_SAVE_INTERVAL_MS;
}

bool ReadingStatsStore::persistToFile(const char* path) const {
  Storage.mkdir("/.crosspoint");
  const bool saved = JsonSettingsIO::saveReadingStats(*this, path);
  if (saved) {
    dirty = false;
    lastSaveMs = millis();
  }
  return saved;
}

void ReadingStatsStore::rebuildSummaryCache() const {
  SummaryCache cache;
  cache.referenceDayOrdinal = getReferenceDayOrdinal();
  cache.goalReadingMs = getDailyReadingGoalMs();

  for (const auto& book : books) {
    cache.totalReadingMs += book.totalReadingMs;
    if (book.completed) {
      cache.booksFinishedCount++;
    }
  }

  if (cache.referenceDayOrdinal != 0) {
    const uint32_t start7DayOrdinal =
        (cache.referenceDayOrdinal >= 6) ? (cache.referenceDayOrdinal - 6) : 0;
    const uint32_t start30DayOrdinal =
        (cache.referenceDayOrdinal >= 29) ? (cache.referenceDayOrdinal - 29) : 0;

    std::vector<uint32_t> eligibleDays;
    eligibleDays.reserve(readingDays.size());

    for (const auto& day : readingDays) {
      if (day.dayOrdinal == cache.referenceDayOrdinal) {
        cache.todayReadingMs = day.readingMs;
      }
      if (day.dayOrdinal >= start7DayOrdinal && day.dayOrdinal <= cache.referenceDayOrdinal) {
        cache.recent7ReadingMs += day.readingMs;
      }
      if (day.dayOrdinal >= start30DayOrdinal && day.dayOrdinal <= cache.referenceDayOrdinal) {
        cache.recent30ReadingMs += day.readingMs;
      }
      if (countsForStreak(day)) {
        eligibleDays.push_back(day.dayOrdinal);
      }
    }

    if (!eligibleDays.empty()) {
      cache.maxStreakDays = 1;
      uint32_t currentMaxStreak = 1;
      for (size_t index = 1; index < eligibleDays.size(); ++index) {
        if (eligibleDays[index] == eligibleDays[index - 1] + 1) {
          currentMaxStreak++;
        } else {
          currentMaxStreak = 1;
        }
        cache.maxStreakDays = std::max(cache.maxStreakDays, currentMaxStreak);
      }

      const uint32_t latestEligibleDay = eligibleDays.back();
      const bool streakIsStillAlive =
          latestEligibleDay == cache.referenceDayOrdinal ||
          (cache.referenceDayOrdinal > 0 && latestEligibleDay + 1 == cache.referenceDayOrdinal);
      if (streakIsStillAlive) {
        cache.currentStreakDays = 1;
        for (size_t index = eligibleDays.size() - 1; index > 0; --index) {
          if (eligibleDays[index] == eligibleDays[index - 1] + 1) {
            cache.currentStreakDays++;
            continue;
          }
          break;
        }
      }
    }
  }

  cache.valid = true;
  summaryCache = cache;
}

void ReadingStatsStore::beginSession(const std::string& path, const std::string& title, const std::string& author,
                                     const std::string& coverBmpPath, const uint8_t progressPercent,
                                     const std::string& chapterTitle, const uint8_t chapterProgressPercent) {
  if (path.empty()) {
    return;
  }

  if (activeSession.active) {
    endSession();
  }

  if (shouldIgnorePath(path)) {
    activeSession = {};
    lastSessionSnapshot = {};
    return;
  }

  size_t index = getOrCreateBookIndex(path, title, author, coverBmpPath);
  touchBook(index);

  auto& book = books[0];
  activeSession.startProgressPercent = book.lastProgressPercent;
  activeSession.startCompleted = book.completed;
  book.lastProgressPercent = clampPercent(progressPercent);
  book.chapterTitle = chapterTitle;
  book.chapterProgressPercent = clampPercent(chapterProgressPercent);
  if (book.lastProgressPercent >= 100) {
    book.completed = true;
  }

  updateBookReadTimestamp(book, TimeUtils::getAuthoritativeTimestamp());

  activeSession.active = true;
  activeSession.bookIndex = 0;
  activeSession.lastInteractionMs = millis();
  activeSession.accumulatedMs = 0;

  markDirty();
}

void ReadingStatsStore::noteActivity() {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    return;
  }

  const unsigned long nowMs = millis();
  const unsigned long elapsedMs = nowMs - activeSession.lastInteractionMs;
  const unsigned long creditedMs = std::min(elapsedMs, MAX_READING_GAP_MS);

  if (creditedMs > 0) {
    auto& book = books[activeSession.bookIndex];
    book.totalReadingMs += creditedMs;
    activeSession.accumulatedMs += creditedMs;
    const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp(), book.lastReadAt);
    recordReadingTime(book, referenceTimestamp, creditedMs);
    updateBookReadTimestamp(book, referenceTimestamp);
    markDirty();
  }

  activeSession.lastInteractionMs = nowMs;
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

void ReadingStatsStore::tickActiveSession() {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    return;
  }

  const unsigned long nowMs = millis();
  if ((nowMs - activeSession.lastInteractionMs) < SESSION_HEARTBEAT_MS) {
    return;
  }

  noteActivity();
}

void ReadingStatsStore::resumeSession() {
  if (!activeSession.active) {
    return;
  }
  activeSession.lastInteractionMs = millis();
}

void ReadingStatsStore::updateProgress(const uint8_t progressPercent, const bool completed, const std::string& chapterTitle,
                                       const uint8_t chapterProgressPercent) {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    return;
  }

  auto& book = books[activeSession.bookIndex];
  const uint8_t clampedBookProgress = clampPercent(progressPercent);
  const uint8_t clampedChapterProgress = clampPercent(chapterProgressPercent);
  const bool progressChanged = book.lastProgressPercent != clampedBookProgress;
  const bool chapterTitleChanged = book.chapterTitle != chapterTitle;
  const bool chapterProgressChanged = book.chapterProgressPercent != clampedChapterProgress;
  const bool completionChanged = !book.completed && (completed || clampedBookProgress >= 100);

  if (!progressChanged && !chapterTitleChanged && !chapterProgressChanged && !completionChanged) {
    return;
  }

  book.lastProgressPercent = clampedBookProgress;
  book.chapterTitle = chapterTitle;
  book.chapterProgressPercent = clampedChapterProgress;
  if (completed || clampedBookProgress >= 100) {
    book.completed = true;
  }

  updateBookReadTimestamp(book, TimeUtils::getAuthoritativeTimestamp());

  markDirty();
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

bool ReadingStatsStore::updateBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                                           const std::string& coverBmpPath) {
  if (shouldIgnorePath(path)) {
    return false;
  }

  auto it = std::find_if(books.begin(), books.end(), [&](const ReadingBookStats& book) { return book.path == path; });
  if (it == books.end()) {
    return false;
  }

  bool changed = false;
  if (!title.empty() && it->title != title) {
    it->title = title;
    changed = true;
  }
  if (!author.empty() && it->author != author) {
    it->author = author;
    changed = true;
  }
  if (!coverBmpPath.empty() && it->coverBmpPath != coverBmpPath) {
    it->coverBmpPath = coverBmpPath;
    changed = true;
  }

  if (changed) {
    markDirty();
    if (shouldSaveDeferred()) {
      saveToFile();
    }
  }
  return changed;
}

bool ReadingStatsStore::removeBook(const std::string& path) {
  auto it = std::find_if(books.begin(), books.end(), [&](const ReadingBookStats& book) { return book.path == path; });
  if (it == books.end()) {
    return false;
  }

  const bool hadBookReadingDays = !it->readingDays.empty();
  const size_t removedIndex = static_cast<size_t>(std::distance(books.begin(), it));
  books.erase(it);

  if (activeSession.active) {
    if (activeSession.bookIndex == removedIndex) {
      activeSession = {};
    } else if (activeSession.bookIndex > removedIndex) {
      activeSession.bookIndex--;
    }
  }

  if (hadBookReadingDays) {
    rebuildAggregatedReadingDays();
  }
  markDirty();
  saveToFile();
  return true;
}

void ReadingStatsStore::endSession() {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    lastSessionSnapshot = {};
    activeSession = {};
    return;
  }

  noteActivity();

  auto& book = books[activeSession.bookIndex];
  const bool countedSession = activeSession.accumulatedMs >= MIN_SESSION_READING_MS;
  const uint32_t sessionMs =
      (activeSession.accumulatedMs > static_cast<uint64_t>(UINT32_MAX)) ? UINT32_MAX
                                                                        : static_cast<uint32_t>(activeSession.accumulatedMs);
  if (countedSession) {
    book.sessions++;
    book.lastSessionMs = sessionMs;
    markDirty();
  }

  lastSessionSnapshot.valid = true;
  lastSessionSnapshot.serial = ++sessionSerialCounter;
  lastSessionSnapshot.path = book.path;
  lastSessionSnapshot.sessionMs = sessionMs;
  lastSessionSnapshot.counted = countedSession;
  lastSessionSnapshot.completedThisSession = !activeSession.startCompleted && book.completed;
  lastSessionSnapshot.startProgressPercent = activeSession.startProgressPercent;
  lastSessionSnapshot.endProgressPercent = book.lastProgressPercent;

  activeSession = {};
  saveToFile();
}

uint32_t ReadingStatsStore::getBooksFinishedCount() const {
  if (!summaryCache.valid || summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.booksFinishedCount;
}

uint64_t ReadingStatsStore::getTotalReadingMs() const {
  if (!summaryCache.valid || summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.totalReadingMs;
}

uint64_t ReadingStatsStore::getTodayReadingMs() const {
  if (!summaryCache.valid || summaryCache.referenceDayOrdinal != getReferenceDayOrdinal() ||
      summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.todayReadingMs;
}

uint64_t ReadingStatsStore::getRecentReadingMs(const uint32_t days) const {
  if (days == 0) {
    return 0;
  }
  if (!summaryCache.valid || summaryCache.referenceDayOrdinal != getReferenceDayOrdinal() ||
      summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  if (days <= 7) {
    return summaryCache.recent7ReadingMs;
  }
  if (days <= 30) {
    return summaryCache.recent30ReadingMs;
  }

  if (readingDays.empty() || summaryCache.referenceDayOrdinal == 0) {
    return 0;
  }

  const uint32_t startDayOrdinal =
      (summaryCache.referenceDayOrdinal >= days - 1) ? (summaryCache.referenceDayOrdinal - (days - 1)) : 0;
  uint64_t totalMs = 0;
  for (const auto& day : readingDays) {
    if (day.dayOrdinal >= startDayOrdinal && day.dayOrdinal <= summaryCache.referenceDayOrdinal) {
      totalMs += day.readingMs;
    }
  }
  return totalMs;
}

uint32_t ReadingStatsStore::getCurrentStreakDays() const {
  if (!summaryCache.valid || summaryCache.referenceDayOrdinal != getReferenceDayOrdinal() ||
      summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.currentStreakDays;
}

uint32_t ReadingStatsStore::getMaxStreakDays() const {
  if (!summaryCache.valid || summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.maxStreakDays;
}

uint32_t ReadingStatsStore::getDisplayTimestamp(bool* usedFallback) const {
  const uint32_t authoritativeTimestamp = TimeUtils::getAuthoritativeTimestamp();
  if (isClockValid(authoritativeTimestamp)) {
    if (usedFallback) {
      *usedFallback = false;
    }
    return authoritativeTimestamp;
  }

  const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
  if (usedFallback) {
    *usedFallback = isClockValid(latestKnownTimestamp);
  }
  return latestKnownTimestamp;
}

void ReadingStatsStore::reset() {
  books.clear();
  legacyReadingDays.clear();
  readingDays.clear();
  activeSession = {};
  lastSessionSnapshot = {};
  markDirty();
  saveToFile();
}

bool ReadingStatsStore::exportToFile(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  return JsonSettingsIO::saveReadingStats(*this, path.c_str());
}

bool ReadingStatsStore::importFromFile(const std::string& path) {
  if (path.empty() || !Storage.exists(path.c_str())) {
    return false;
  }

  const bool loaded = JsonSettingsIO::loadReadingStatsFromFile(*this, path.c_str());
  if (!loaded) {
    return false;
  }

  normalizeReadingDays(readingDays);
  for (auto& book : books) {
    normalizeReadingDays(book.readingDays);
  }
  activeSession = {};
  lastSessionSnapshot = {};
  sessionSerialCounter = 0;
  removeIgnoredBooks();
  rebuildAggregatedReadingDays();
  const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
  if (isClockValid(latestKnownTimestamp) && latestKnownTimestamp > APP_STATE.lastKnownValidTimestamp) {
    APP_STATE.lastKnownValidTimestamp = latestKnownTimestamp;
    APP_STATE.saveToFile();
  }
  markDirty();
  return saveToFile();
}

bool ReadingStatsStore::saveToFile() const {
  if (!dirty && Storage.exists(READING_STATS_FILE_JSON)) {
    return true;
  }
  if (activeSession.active && !shouldSaveDeferred()) {
    return true;
  }
  return persistToFile(READING_STATS_FILE_JSON);
}

bool ReadingStatsStore::loadFromFile() {
  if (!Storage.exists(READING_STATS_FILE_JSON)) {
    return false;
  }

  const bool loaded = JsonSettingsIO::loadReadingStatsFromFile(*this, READING_STATS_FILE_JSON);
  if (loaded) {
    // Pre-reserve vector capacities to reduce heap fragmentation
    // Reserve reasonable initial sizes based on typical usage
    books.reserve(std::max<size_t>(books.size() + 10, 50));
    readingDays.reserve(std::max<size_t>(readingDays.size() + 30, 365));
    legacyReadingDays.reserve(std::max<size_t>(legacyReadingDays.size() + 30, 365));

    normalizeReadingDays(readingDays);
    for (auto& book : books) {
      book.readingDays.reserve(std::max<size_t>(book.readingDays.size() + 7, 30));
      normalizeReadingDays(book.readingDays);
    }
    removeIgnoredBooks();
    rebuildAggregatedReadingDays();
    const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
    if (isClockValid(latestKnownTimestamp) && latestKnownTimestamp > APP_STATE.lastKnownValidTimestamp) {
      APP_STATE.lastKnownValidTimestamp = latestKnownTimestamp;
    }
    activeSession = {};
    lastSessionSnapshot = {};
    sessionSerialCounter = 0;
    dirty = false;
    lastSaveMs = millis();
    invalidateSummaryCache();
  }
  return loaded;
}
