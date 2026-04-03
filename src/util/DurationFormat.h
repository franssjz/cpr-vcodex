#pragma once

#include <cstdint>
#include <string>

namespace DurationFormat {

/**
 * @brief Format milliseconds as human-readable "Xh Ym" or "Xm" string.
 * @param totalMs Duration in milliseconds
 * @return Formatted duration string (e.g., "1h 30m", "45m", "0m")
 */
inline std::string formatHm(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

}  // namespace DurationFormat
