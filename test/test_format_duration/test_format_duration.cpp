/**
 * @file test_format_duration.cpp
 * @brief Unit tests for DurationFormat::formatHm (shared implementation)
 *
 * Tests duration formatting (milliseconds → human-readable "Xh Ym" format).
 * Uses the production implementation from src/util/DurationFormat.h directly.
 */

#include <unity.h>

#include <cstdint>
#include <string>

#include "../../src/util/DurationFormat.h"

// ── Zero and small values ───────────────────────────────────────────

void test_format_zero_ms() { TEST_ASSERT_EQUAL_STRING("0m", DurationFormat::formatHm(0).c_str()); }

void test_format_sub_minute() {
  // 30 seconds = 30000 ms → 0 minutes
  TEST_ASSERT_EQUAL_STRING("0m", DurationFormat::formatHm(30000).c_str());
}

void test_format_exactly_one_minute() { TEST_ASSERT_EQUAL_STRING("1m", DurationFormat::formatHm(60000).c_str()); }

void test_format_59_seconds() {
  // 59999ms → 0 minutes (truncation)
  TEST_ASSERT_EQUAL_STRING("0m", DurationFormat::formatHm(59999).c_str());
}

// ── Minutes only (< 1 hour) ────────────────────────────────────────

void test_format_5_minutes() { TEST_ASSERT_EQUAL_STRING("5m", DurationFormat::formatHm(5 * 60000ULL).c_str()); }

void test_format_15_minutes() { TEST_ASSERT_EQUAL_STRING("15m", DurationFormat::formatHm(15 * 60000ULL).c_str()); }

void test_format_30_minutes() { TEST_ASSERT_EQUAL_STRING("30m", DurationFormat::formatHm(30 * 60000ULL).c_str()); }

void test_format_59_minutes() { TEST_ASSERT_EQUAL_STRING("59m", DurationFormat::formatHm(59 * 60000ULL).c_str()); }

// ── Hours and minutes ───────────────────────────────────────────────

void test_format_exactly_one_hour() {
  TEST_ASSERT_EQUAL_STRING("1h 0m", DurationFormat::formatHm(60 * 60000ULL).c_str());
}

void test_format_one_hour_30_minutes() {
  TEST_ASSERT_EQUAL_STRING("1h 30m", DurationFormat::formatHm(90 * 60000ULL).c_str());
}

void test_format_2_hours_15_minutes() {
  uint64_t ms = (2 * 60 + 15) * 60000ULL;
  TEST_ASSERT_EQUAL_STRING("2h 15m", DurationFormat::formatHm(ms).c_str());
}

void test_format_10_hours_0_minutes() {
  TEST_ASSERT_EQUAL_STRING("10h 0m", DurationFormat::formatHm(600 * 60000ULL).c_str());
}

void test_format_24_hours() { TEST_ASSERT_EQUAL_STRING("24h 0m", DurationFormat::formatHm(1440 * 60000ULL).c_str()); }

void test_format_100_hours() {
  // Large value: 100 hours
  uint64_t ms = 100ULL * 60 * 60000ULL;
  TEST_ASSERT_EQUAL_STRING("100h 0m", DurationFormat::formatHm(ms).c_str());
}

// ── Edge cases ──────────────────────────────────────────────────────

void test_format_1_ms() { TEST_ASSERT_EQUAL_STRING("0m", DurationFormat::formatHm(1).c_str()); }

void test_format_daily_goal_15_min() {
  // Typical daily goal: 15 minutes
  TEST_ASSERT_EQUAL_STRING("15m", DurationFormat::formatHm(15 * 60000ULL).c_str());
}

void test_format_daily_goal_30_min() {
  TEST_ASSERT_EQUAL_STRING("30m", DurationFormat::formatHm(30 * 60000ULL).c_str());
}

void test_format_daily_goal_60_min() {
  TEST_ASSERT_EQUAL_STRING("1h 0m", DurationFormat::formatHm(60 * 60000ULL).c_str());
}

void test_format_partial_minutes_truncated() {
  // 90.5 minutes = 90 * 60000 + 30000 = 5430000 ms → "1h 30m" (30 seconds ignored)
  TEST_ASSERT_EQUAL_STRING("1h 30m", DurationFormat::formatHm(5430000).c_str());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

  // Zero and small values
  RUN_TEST(test_format_zero_ms);
  RUN_TEST(test_format_sub_minute);
  RUN_TEST(test_format_exactly_one_minute);
  RUN_TEST(test_format_59_seconds);

  // Minutes only
  RUN_TEST(test_format_5_minutes);
  RUN_TEST(test_format_15_minutes);
  RUN_TEST(test_format_30_minutes);
  RUN_TEST(test_format_59_minutes);

  // Hours and minutes
  RUN_TEST(test_format_exactly_one_hour);
  RUN_TEST(test_format_one_hour_30_minutes);
  RUN_TEST(test_format_2_hours_15_minutes);
  RUN_TEST(test_format_10_hours_0_minutes);
  RUN_TEST(test_format_24_hours);
  RUN_TEST(test_format_100_hours);

  // Edge cases
  RUN_TEST(test_format_1_ms);
  RUN_TEST(test_format_daily_goal_15_min);
  RUN_TEST(test_format_daily_goal_30_min);
  RUN_TEST(test_format_daily_goal_60_min);
  RUN_TEST(test_format_partial_minutes_truncated);

  return UNITY_END();
}
