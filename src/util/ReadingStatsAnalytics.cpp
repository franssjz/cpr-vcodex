#include "ReadingStatsAnalytics.h"

#include <I18n.h>

#include <algorithm>
#include <ctime>

#include "util/TimeUtils.h"

namespace ReadingStatsAnalytics {
namespace {
constexpr uint64_t MIN_READING_DAY_BOOK_MS = 3ULL * 60ULL * 1000ULL;
constexpr uint64_t MIN_ESTIMATE_READING_MS = 10ULL * 60ULL * 1000ULL;
constexpr uint64_t MIN_CHAPTER_ESTIMATE_READING_MS = 2ULL * 60ULL * 1000ULL;
constexpr uint64_t MIN_ESTIMATE_AVG_SESSION_MS = 5ULL * 60ULL * 1000ULL;
constexpr uint64_t ESTIMATE_ROUNDING_MS = 5ULL * 60ULL * 1000ULL;
constexpr uint8_t MIN_ESTIMATE_PROGRESS_PERCENT = 5;
constexpr uint8_t MIN_CHAPTER_ESTIMATE_PROGRESS_DELTA = 5;

int resolveYearFromTimestamp(const uint32_t timestamp) {
  if (!TimeUtils::isClockValid(timestamp)) {
    return 0;
  }

  time_t currentTime = static_cast<time_t>(timestamp);
  tm localTime = {};
  if (localtime_r(&currentTime, &localTime) == nullptr) {
    return 0;
  }
  return localTime.tm_year + 1900;
}

uint64_t roundUpEstimateMs(const uint64_t valueMs) {
  if (valueMs == 0) {
    return 0;
  }
  return ((valueMs + ESTIMATE_ROUNDING_MS - 1) / ESTIMATE_ROUNDING_MS) * ESTIMATE_ROUNDING_MS;
}

uint32_t calculateSessionsLeft(const uint64_t remainingMs, const uint64_t averageSessionMs) {
  if (remainingMs == 0 || averageSessionMs < MIN_ESTIMATE_AVG_SESSION_MS) {
    return 0;
  }
  return static_cast<uint32_t>((remainingMs + averageSessionMs - 1) / averageSessionMs);
}

}  // namespace

std::string formatDurationHm(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string formatDayOrdinalLabel(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return "";
  }

  return TimeUtils::formatDateParts(year, month, day);
}

std::string formatMonthLabel(const int year, const unsigned month) { return TimeUtils::formatMonthYear(year, month); }

int getReferenceYear() {
  const uint32_t timestamp = READING_STATS.getDisplayTimestamp();
  if (const int year = resolveYearFromTimestamp(timestamp); year != 0) {
    return year;
  }

  if (!READING_STATS.getReadingDays().empty()) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (TimeUtils::getDateFromDayOrdinal(READING_STATS.getReadingDays().back().dayOrdinal, year, month, day)) {
      return year;
    }
  }

  return 2026;
}

uint64_t getAverageSessionMs() {
  uint64_t totalReadingMs = 0;
  uint32_t sessionCount = 0;
  for (const auto& book : READING_STATS.getBooks()) {
    totalReadingMs += book.totalReadingMs;
    sessionCount += book.sessions;
  }
  return sessionCount == 0 ? 0 : totalReadingMs / sessionCount;
}

uint64_t getAverageReadingDayMs() {
  uint64_t totalReadingMs = 0;
  uint32_t readingDayCount = 0;
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.readingMs == 0) {
      continue;
    }
    totalReadingMs += day.readingMs;
    readingDayCount++;
  }
  return readingDayCount == 0 ? 0 : totalReadingMs / readingDayCount;
}

TimeLeftEstimate buildBookTimeLeftEstimate(const ReadingBookStats& book) {
  TimeLeftEstimate estimate;
  if (book.completed || book.lastProgressPercent >= 100) {
    estimate.completed = true;
    estimate.ready = true;
    return estimate;
  }

  if (book.totalReadingMs < MIN_ESTIMATE_READING_MS || book.lastProgressPercent < MIN_ESTIMATE_PROGRESS_PERCENT) {
    return estimate;
  }

  const uint64_t estimatedTotalMs =
      (book.totalReadingMs * 100ULL + book.lastProgressPercent - 1) / book.lastProgressPercent;
  if (estimatedTotalMs <= book.totalReadingMs) {
    return estimate;
  }

  estimate.remainingMs = roundUpEstimateMs(estimatedTotalMs - book.totalReadingMs);
  estimate.sessionsLeft = calculateSessionsLeft(estimate.remainingMs, book.sessions == 0 ? 0 : book.totalReadingMs / book.sessions);
  estimate.ready = true;
  return estimate;
}

TimeLeftEstimate buildChapterTimeLeftEstimate(const ReadingBookStats& book) {
  TimeLeftEstimate estimate;
  if (book.completed || book.lastProgressPercent >= 100 || book.chapterProgressPercent >= 100) {
    estimate.completed = true;
    estimate.ready = true;
    return estimate;
  }

  if (book.chapterTitle.empty() || book.currentChapterReadingMs < MIN_CHAPTER_ESTIMATE_READING_MS ||
      book.chapterProgressPercent <= book.chapterReadingStartProgressPercent) {
    return estimate;
  }

  const uint8_t progressDelta = book.chapterProgressPercent - book.chapterReadingStartProgressPercent;
  const uint8_t remainingProgress = 100 - book.chapterProgressPercent;
  if (progressDelta < MIN_CHAPTER_ESTIMATE_PROGRESS_DELTA || remainingProgress == 0) {
    return estimate;
  }

  estimate.remainingMs = roundUpEstimateMs((book.currentChapterReadingMs * remainingProgress + progressDelta - 1) /
                                           progressDelta);
  estimate.sessionsLeft = calculateSessionsLeft(estimate.remainingMs, book.sessions == 0 ? 0 : book.totalReadingMs / book.sessions);
  estimate.ready = true;
  return estimate;
}

std::string formatTimeLeftEstimate(const TimeLeftEstimate& estimate) {
  if (estimate.completed) {
    return tr(STR_DONE);
  }
  if (!estimate.ready || estimate.remainingMs == 0) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }

  std::string value = "~" + formatDurationHm(estimate.remainingMs);
  if (estimate.sessionsLeft > 0) {
    value += " / " + std::to_string(estimate.sessionsLeft) + " " +
             (estimate.sessionsLeft == 1 ? std::string(tr(STR_SESSION)) : std::string(tr(STR_SESSIONS)));
  }
  return value;
}

std::vector<DayBookEntry> getBooksReadOnDay(const uint32_t dayOrdinal) {
  std::vector<DayBookEntry> entries;
  for (const auto& book : READING_STATS.getBooks()) {
    auto it = std::find_if(book.readingDays.begin(), book.readingDays.end(), [&](const ReadingDayStats& day) {
      return day.dayOrdinal == dayOrdinal && day.readingMs >= MIN_READING_DAY_BOOK_MS;
    });
    if (it == book.readingDays.end()) {
      continue;
    }

    entries.push_back(DayBookEntry{&book, it->readingMs});
  }

  std::sort(entries.begin(), entries.end(), [](const DayBookEntry& left, const DayBookEntry& right) {
    if (left.readingMs != right.readingMs) {
      return left.readingMs > right.readingMs;
    }
    if (!left.book || !right.book) {
      return left.book != nullptr;
    }
    return left.book->title < right.book->title;
  });
  return entries;
}

TimelineDayEntry buildTimelineDayEntry(const uint32_t dayOrdinal) {
  TimelineDayEntry entry;
  entry.dayOrdinal = dayOrdinal;
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.dayOrdinal == dayOrdinal) {
      entry.totalReadingMs = day.readingMs;
      break;
    }
  }

  const auto books = getBooksReadOnDay(dayOrdinal);
  entry.booksReadCount = static_cast<uint32_t>(books.size());
  if (!books.empty()) {
    entry.topBook = books.front().book;
    entry.topBookReadingMs = books.front().readingMs;
  }
  return entry;
}

std::vector<TimelineDayEntry> buildTimelineEntries(const size_t maxEntries) {
  std::vector<TimelineDayEntry> entries;
  const auto& readingDays = READING_STATS.getReadingDays();
  entries.reserve(readingDays.size());

  for (auto it = readingDays.rbegin(); it != readingDays.rend(); ++it) {
    if (it->readingMs == 0) {
      continue;
    }
    entries.push_back(buildTimelineDayEntry(it->dayOrdinal));
    if (maxEntries > 0 && entries.size() >= maxEntries) {
      break;
    }
  }
  return entries;
}

}  // namespace ReadingStatsAnalytics
