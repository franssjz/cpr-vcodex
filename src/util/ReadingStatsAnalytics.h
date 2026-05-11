#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"

namespace ReadingStatsAnalytics {

struct DayBookEntry {
  const ReadingBookStats* book = nullptr;
  uint64_t readingMs = 0;
};

struct TimelineDayEntry {
  uint32_t dayOrdinal = 0;
  uint64_t totalReadingMs = 0;
  uint32_t booksReadCount = 0;
  const ReadingBookStats* topBook = nullptr;
  uint64_t topBookReadingMs = 0;
};

enum class EstimateConfidence : uint8_t { LOW_CONFIDENCE = 0, MEDIUM_CONFIDENCE = 1, HIGH_CONFIDENCE = 2 };

struct TimeLeftEstimate {
  bool ready = false;
  bool completed = false;
  uint64_t remainingMs = 0;
  EstimateConfidence confidence = EstimateConfidence::LOW_CONFIDENCE;
  uint32_t trackedProgressDeltaPercent = 0;
  uint64_t trackedProgressMs = 0;
  uint32_t progressPerHourTenths = 0;
};

std::string formatDurationHm(uint64_t totalMs);
std::string formatDayOrdinalLabel(uint32_t dayOrdinal);
std::string formatMonthLabel(int year, unsigned month);
int getReferenceYear();
uint64_t getAverageSessionMs();
uint64_t getAverageReadingDayMs();
TimeLeftEstimate buildBookTimeLeftEstimate(const ReadingBookStats& book);
TimeLeftEstimate buildChapterTimeLeftEstimate(const ReadingBookStats& book);
std::string formatTimeLeftEstimate(const TimeLeftEstimate& estimate);
std::string formatCompactTimeLeftEstimate(const TimeLeftEstimate& estimate);
std::string formatProgressPace(uint32_t progressPerHourTenths);
std::string formatEstimateConfidence(EstimateConfidence confidence);
std::string formatPaceTrend(const ReadingBookStats& book);
uint32_t getAverageProgressPaceTenths(const ReadingBookStats& book);
uint32_t getRecentProgressPaceTenths(const ReadingBookStats& book);
uint32_t getTrackedProgressGainPercent(const ReadingBookStats& book);
std::vector<DayBookEntry> getBooksReadOnDay(uint32_t dayOrdinal);
TimelineDayEntry buildTimelineDayEntry(uint32_t dayOrdinal);
std::vector<TimelineDayEntry> buildTimelineEntries(size_t maxEntries = 0);

}  // namespace ReadingStatsAnalytics
