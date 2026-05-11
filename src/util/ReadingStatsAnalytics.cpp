#include "ReadingStatsAnalytics.h"

#include <I18n.h>

#include <algorithm>
#include <ctime>

#include "util/TimeUtils.h"

namespace ReadingStatsAnalytics {
namespace {
constexpr uint64_t MIN_READING_DAY_BOOK_MS = 3ULL * 60ULL * 1000ULL;
constexpr uint64_t MIN_MEDIUM_CONFIDENCE_MS = 20ULL * 60ULL * 1000ULL;
constexpr uint64_t MIN_HIGH_CONFIDENCE_MS = 60ULL * 60ULL * 1000ULL;
constexpr uint64_t ESTIMATE_ROUNDING_MS = 5ULL * 60ULL * 1000ULL;
constexpr uint32_t MIN_MEDIUM_CONFIDENCE_PROGRESS = 3;
constexpr uint32_t MIN_HIGH_CONFIDENCE_PROGRESS = 10;
constexpr uint32_t RECENT_SAMPLE_COUNT = 8;
constexpr uint32_t TREND_SAMPLE_COUNT = 4;

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

std::string formatDurationCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h" + (minutes < 10 ? "0" : "") + std::to_string(minutes);
}

EstimateConfidence classifyConfidence(const uint64_t trackedMs, const uint32_t progressDeltaPercent) {
  if (trackedMs >= MIN_HIGH_CONFIDENCE_MS || progressDeltaPercent >= MIN_HIGH_CONFIDENCE_PROGRESS) {
    return EstimateConfidence::HIGH_CONFIDENCE;
  }
  if ((trackedMs >= MIN_MEDIUM_CONFIDENCE_MS && progressDeltaPercent >= MIN_MEDIUM_CONFIDENCE_PROGRESS) ||
      progressDeltaPercent >= 5) {
    return EstimateConfidence::MEDIUM_CONFIDENCE;
  }
  return EstimateConfidence::LOW_CONFIDENCE;
}

TimeLeftEstimate buildProgressDeltaEstimate(const ReadingBookStats& book, const uint8_t currentProgressPercent) {
  TimeLeftEstimate estimate;
  if (book.completed || currentProgressPercent >= 100) {
    estimate.completed = true;
    estimate.ready = true;
    estimate.confidence = EstimateConfidence::HIGH_CONFIDENCE;
    return estimate;
  }

  if (book.progressSamples.empty()) {
    return estimate;
  }

  uint64_t trackedMs = 0;
  uint32_t progressDelta = 0;
  uint32_t samplesUsed = 0;
  for (auto it = book.progressSamples.rbegin(); it != book.progressSamples.rend() && samplesUsed < RECENT_SAMPLE_COUNT; ++it) {
    if (it->sessionMs == 0 || it->endProgressPercent <= it->startProgressPercent) {
      continue;
    }
    trackedMs += it->sessionMs;
    progressDelta += static_cast<uint32_t>(it->endProgressPercent - it->startProgressPercent);
    samplesUsed++;
  }

  if (trackedMs == 0 || progressDelta == 0) {
    return estimate;
  }

  estimate.confidence = classifyConfidence(trackedMs, progressDelta);
  estimate.trackedProgressDeltaPercent = progressDelta;
  estimate.trackedProgressMs = trackedMs;
  estimate.progressPerHourTenths =
      static_cast<uint32_t>((static_cast<uint64_t>(progressDelta) * 36000ULL + trackedMs / 2) / trackedMs);
  if (estimate.confidence == EstimateConfidence::LOW_CONFIDENCE) {
    return estimate;
  }

  const uint32_t remainingPercent = 100 - currentProgressPercent;
  estimate.remainingMs =
      roundUpEstimateMs((static_cast<uint64_t>(remainingPercent) * trackedMs + progressDelta - 1) / progressDelta);
  estimate.ready = estimate.remainingMs > 0;
  return estimate;
}

uint32_t buildPaceTenths(const ReadingBookStats& book, const uint32_t maxSamples) {
  uint64_t trackedMs = 0;
  uint32_t progressDelta = 0;
  uint32_t samplesUsed = 0;
  for (auto it = book.progressSamples.rbegin(); it != book.progressSamples.rend(); ++it) {
    if (maxSamples > 0 && samplesUsed >= maxSamples) {
      break;
    }
    if (it->sessionMs == 0 || it->endProgressPercent <= it->startProgressPercent) {
      continue;
    }
    trackedMs += it->sessionMs;
    progressDelta += static_cast<uint32_t>(it->endProgressPercent - it->startProgressPercent);
    samplesUsed++;
  }
  if (trackedMs == 0 || progressDelta == 0) {
    return 0;
  }
  return static_cast<uint32_t>((static_cast<uint64_t>(progressDelta) * 36000ULL + trackedMs / 2) / trackedMs);
}

uint32_t buildPaceTenthsWindow(const ReadingBookStats& book, const uint32_t skipSamples, const uint32_t maxSamples) {
  uint64_t trackedMs = 0;
  uint32_t progressDelta = 0;
  uint32_t validSamplesSeen = 0;
  uint32_t samplesUsed = 0;
  for (auto it = book.progressSamples.rbegin(); it != book.progressSamples.rend(); ++it) {
    if (it->sessionMs == 0 || it->endProgressPercent <= it->startProgressPercent) {
      continue;
    }
    if (validSamplesSeen++ < skipSamples) {
      continue;
    }
    if (maxSamples > 0 && samplesUsed >= maxSamples) {
      break;
    }
    trackedMs += it->sessionMs;
    progressDelta += static_cast<uint32_t>(it->endProgressPercent - it->startProgressPercent);
    samplesUsed++;
  }
  if (trackedMs == 0 || progressDelta == 0) {
    return 0;
  }
  return static_cast<uint32_t>((static_cast<uint64_t>(progressDelta) * 36000ULL + trackedMs / 2) / trackedMs);
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
  return buildProgressDeltaEstimate(book, book.lastProgressPercent);
}

TimeLeftEstimate buildChapterTimeLeftEstimate(const ReadingBookStats& book) {
  TimeLeftEstimate estimate;
  if (book.completed || book.lastProgressPercent >= 100 || book.chapterProgressPercent >= 100) {
    estimate.completed = true;
    estimate.ready = true;
    estimate.confidence = EstimateConfidence::HIGH_CONFIDENCE;
    return estimate;
  }

  if (book.chapterTitle.empty() || book.chapterProgressPercent <= book.chapterReadingStartProgressPercent) {
    return estimate;
  }

  const uint8_t progressDelta = book.chapterProgressPercent - book.chapterReadingStartProgressPercent;
  const uint8_t remainingProgress = 100 - book.chapterProgressPercent;
  if (progressDelta < MIN_MEDIUM_CONFIDENCE_PROGRESS || remainingProgress == 0 || book.currentChapterReadingMs == 0) {
    return estimate;
  }

  estimate.confidence = classifyConfidence(book.currentChapterReadingMs, progressDelta);
  estimate.trackedProgressDeltaPercent = progressDelta;
  estimate.trackedProgressMs = book.currentChapterReadingMs;
  estimate.progressPerHourTenths =
      static_cast<uint32_t>((static_cast<uint64_t>(progressDelta) * 36000ULL + book.currentChapterReadingMs / 2) /
                            book.currentChapterReadingMs);
  if (estimate.confidence == EstimateConfidence::LOW_CONFIDENCE) {
    return estimate;
  }

  estimate.remainingMs = roundUpEstimateMs((book.currentChapterReadingMs * remainingProgress + progressDelta - 1) /
                                           progressDelta);
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

  return "~" + formatDurationHm(estimate.remainingMs);
}

std::string formatCompactTimeLeftEstimate(const TimeLeftEstimate& estimate) {
  if (estimate.completed) {
    return tr(STR_DONE);
  }
  if (!estimate.ready || estimate.remainingMs == 0) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }
  return "~" + formatDurationCompact(estimate.remainingMs);
}

std::string formatEstimateReadinessExplanation(const TimeLeftEstimate& estimate) {
  if (estimate.completed || estimate.ready) {
    return "";
  }

  const uint32_t neededProgress =
      estimate.trackedProgressDeltaPercent >= MIN_MEDIUM_CONFIDENCE_PROGRESS
          ? 0
          : MIN_MEDIUM_CONFIDENCE_PROGRESS - estimate.trackedProgressDeltaPercent;
  const uint64_t neededMs =
      estimate.trackedProgressMs >= MIN_MEDIUM_CONFIDENCE_MS ? 0 : MIN_MEDIUM_CONFIDENCE_MS - estimate.trackedProgressMs;

  if (neededProgress > 0 && neededMs > 0) {
    return std::string(tr(STR_NEEDS_PREFIX)) + std::to_string(neededProgress) + "%" +
           tr(STR_MORE_PROGRESS_SUFFIX) + " / " + formatDurationCompact(neededMs) + tr(STR_TRACKED_SUFFIX);
  }
  if (neededProgress > 0) {
    return std::string(tr(STR_NEEDS_PREFIX)) + std::to_string(neededProgress) + "%" +
           tr(STR_MORE_PROGRESS_SUFFIX);
  }
  if (neededMs > 0) {
    return std::string(tr(STR_NEEDS_PREFIX)) + formatDurationCompact(neededMs) + tr(STR_TRACKED_SUFFIX);
  }
  return tr(STR_ESTIMATE_AFTER_MORE_READING);
}

std::string formatProgressPace(const uint32_t progressPerHourTenths) {
  if (progressPerHourTenths == 0) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }
  return std::to_string(progressPerHourTenths / 10) + "." + std::to_string(progressPerHourTenths % 10) + "%/h";
}

std::string formatEstimateConfidence(const EstimateConfidence confidence) {
  switch (confidence) {
    case EstimateConfidence::HIGH_CONFIDENCE:
      return tr(STR_HIGH_CONFIDENCE);
    case EstimateConfidence::MEDIUM_CONFIDENCE:
      return tr(STR_MEDIUM_CONFIDENCE);
    case EstimateConfidence::LOW_CONFIDENCE:
    default:
      return tr(STR_LOW_CONFIDENCE);
  }
}

uint32_t getAverageProgressPaceTenths(const ReadingBookStats& book) { return buildPaceTenths(book, 0); }

uint32_t getRecentProgressPaceTenths(const ReadingBookStats& book) { return buildPaceTenths(book, RECENT_SAMPLE_COUNT); }

uint32_t getOlderProgressPaceTenths(const ReadingBookStats& book) {
  return buildPaceTenthsWindow(book, TREND_SAMPLE_COUNT, TREND_SAMPLE_COUNT);
}

std::string formatEstimateStability(const ReadingBookStats& book) {
  if (book.estimateSamples.size() < 3) {
    return tr(STR_NOT_ENOUGH);
  }

  uint64_t totalSwing = 0;
  uint32_t comparisons = 0;
  for (size_t index = 1; index < book.estimateSamples.size(); ++index) {
    const uint32_t previous = book.estimateSamples[index - 1].remainingMs;
    const uint32_t current = book.estimateSamples[index].remainingMs;
    if (previous == 0 || current == 0) {
      continue;
    }
    totalSwing += previous > current ? previous - current : current - previous;
    comparisons++;
  }
  if (comparisons == 0) {
    return tr(STR_NOT_ENOUGH);
  }

  const uint32_t latest = book.estimateSamples.back().remainingMs;
  const uint64_t averageSwing = totalSwing / comparisons;
  if (averageSwing * 100ULL <= static_cast<uint64_t>(latest) * 20ULL) {
    return tr(STR_ESTIMATE_STABILIZING);
  }
  return tr(STR_ESTIMATE_JUMPY);
}

std::string formatPaceTrend(const ReadingBookStats& book) {
  const uint32_t older = getOlderProgressPaceTenths(book);
  const uint32_t recent = buildPaceTenthsWindow(book, 0, TREND_SAMPLE_COUNT);
  if (older == 0 || recent == 0) {
    return tr(STR_NOT_ENOUGH);
  }

  const uint32_t delta = older > recent ? older - recent : recent - older;
  if (delta * 100 < older * 15) {
    return tr(STR_PACE_STEADY);
  }

  return recent > older ? tr(STR_PACE_FASTER_LATELY) : tr(STR_PACE_SLOWER_LATELY);
}

uint32_t getTrackedProgressGainPercent(const ReadingBookStats& book) {
  uint32_t progressDelta = 0;
  for (const auto& sample : book.progressSamples) {
    if (sample.endProgressPercent > sample.startProgressPercent) {
      progressDelta += static_cast<uint32_t>(sample.endProgressPercent - sample.startProgressPercent);
    }
  }
  return progressDelta;
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
