/**
 * @file test_date_math.cpp
 * @brief Unit tests for TimeUtils date math functions
 *
 * Tests getDayOrdinalForDate, getDateFromDayOrdinal, isClockValid,
 * and date formatting functions. Uses mocked ESP32 dependencies.
 */

#include <unity.h>

#include <cstdint>
#include <string>

// Include source files under test (mocks provide ESP32 stubs)
#include "../../src/util/TimeZoneRegistry.cpp"
#include "../../src/util/TimeUtils.cpp"

// Provide the static instance required by CrossPointSettings
CrossPointSettings CrossPointSettings::instance;

// ── isClockValid ────────────────────────────────────────────────────

void test_clock_valid_recent_timestamp() {
  // 2024-06-15 12:00:00 UTC = 1718452800
  TEST_ASSERT_TRUE(TimeUtils::isClockValid(1718452800));
}

void test_clock_valid_exactly_at_threshold() {
  // 2024-01-01 00:00:00 UTC = 1704067200 (exact threshold)
  TEST_ASSERT_TRUE(TimeUtils::isClockValid(1704067200));
}

void test_clock_invalid_zero() {
  TEST_ASSERT_FALSE(TimeUtils::isClockValid(0));
}

void test_clock_invalid_old_timestamp() {
  // 2020-01-01 = 1577836800 (before threshold)
  TEST_ASSERT_FALSE(TimeUtils::isClockValid(1577836800));
}

void test_clock_invalid_just_before_threshold() {
  TEST_ASSERT_FALSE(TimeUtils::isClockValid(1704067199));
}

void test_clock_valid_far_future() {
  // 2030-01-01 = 1893456000
  TEST_ASSERT_TRUE(TimeUtils::isClockValid(1893456000));
}

// ── getDayOrdinalForDate / getDateFromDayOrdinal (round-trip) ───────

void test_day_ordinal_unix_epoch() {
  // 1970-01-01 should give day ordinal 0
  uint32_t ordinal = TimeUtils::getDayOrdinalForDate(1970, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(0, ordinal);
}

void test_day_ordinal_round_trip_2024_01_01() {
  uint32_t ordinal = TimeUtils::getDayOrdinalForDate(2024, 1, 1);
  int year;
  unsigned month, day;
  TEST_ASSERT_TRUE(TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day));
  TEST_ASSERT_EQUAL(2024, year);
  TEST_ASSERT_EQUAL(1, month);
  TEST_ASSERT_EQUAL(1, day);
}

void test_day_ordinal_round_trip_2026_04_03() {
  uint32_t ordinal = TimeUtils::getDayOrdinalForDate(2026, 4, 3);
  int year;
  unsigned month, day;
  TEST_ASSERT_TRUE(TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day));
  TEST_ASSERT_EQUAL(2026, year);
  TEST_ASSERT_EQUAL(4, month);
  TEST_ASSERT_EQUAL(3, day);
}

void test_day_ordinal_leap_year_feb29() {
  // Feb 29 in a leap year
  uint32_t ordinal = TimeUtils::getDayOrdinalForDate(2024, 2, 29);
  int year;
  unsigned month, day;
  TEST_ASSERT_TRUE(TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day));
  TEST_ASSERT_EQUAL(2024, year);
  TEST_ASSERT_EQUAL(2, month);
  TEST_ASSERT_EQUAL(29, day);
}

void test_day_ordinal_leap_year_mar1_after_feb29() {
  uint32_t feb29 = TimeUtils::getDayOrdinalForDate(2024, 2, 29);
  uint32_t mar1 = TimeUtils::getDayOrdinalForDate(2024, 3, 1);
  TEST_ASSERT_EQUAL_UINT32(feb29 + 1, mar1);
}

void test_day_ordinal_non_leap_year_feb28_to_mar1() {
  uint32_t feb28 = TimeUtils::getDayOrdinalForDate(2025, 2, 28);
  uint32_t mar1 = TimeUtils::getDayOrdinalForDate(2025, 3, 1);
  TEST_ASSERT_EQUAL_UINT32(feb28 + 1, mar1);
}

void test_day_ordinal_consecutive_days() {
  uint32_t day1 = TimeUtils::getDayOrdinalForDate(2026, 4, 1);
  uint32_t day2 = TimeUtils::getDayOrdinalForDate(2026, 4, 2);
  uint32_t day3 = TimeUtils::getDayOrdinalForDate(2026, 4, 3);
  TEST_ASSERT_EQUAL_UINT32(day1 + 1, day2);
  TEST_ASSERT_EQUAL_UINT32(day2 + 1, day3);
}

void test_day_ordinal_year_boundary() {
  uint32_t dec31 = TimeUtils::getDayOrdinalForDate(2025, 12, 31);
  uint32_t jan1 = TimeUtils::getDayOrdinalForDate(2026, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(dec31 + 1, jan1);
}

void test_day_ordinal_month_lengths() {
  // Test various month lengths
  uint32_t jan31 = TimeUtils::getDayOrdinalForDate(2026, 1, 31);
  uint32_t feb1 = TimeUtils::getDayOrdinalForDate(2026, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(30, jan31 - feb1);  // January has 31 days
}

void test_day_ordinal_full_year_365() {
  // 2025 is a non-leap year: 365 days
  uint32_t jan1_2025 = TimeUtils::getDayOrdinalForDate(2025, 1, 1);
  uint32_t jan1_2026 = TimeUtils::getDayOrdinalForDate(2026, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(365, jan1_2026 - jan1_2025);
}

void test_day_ordinal_leap_year_366() {
  // 2024 is a leap year: 366 days
  uint32_t jan1_2024 = TimeUtils::getDayOrdinalForDate(2024, 1, 1);
  uint32_t jan1_2025 = TimeUtils::getDayOrdinalForDate(2025, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(366, jan1_2025 - jan1_2024);
}

void test_day_ordinal_century_leap_rules() {
  // 2000 was a leap year (divisible by 400)
  uint32_t feb29_2000 = TimeUtils::getDayOrdinalForDate(2000, 2, 29);
  int year;
  unsigned month, day;
  TimeUtils::getDateFromDayOrdinal(feb29_2000, year, month, day);
  TEST_ASSERT_EQUAL(2000, year);
  TEST_ASSERT_EQUAL(2, month);
  TEST_ASSERT_EQUAL(29, day);
}

void test_day_ordinal_round_trip_many_dates() {
  // Test a variety of dates across different years
  struct TestDate {
    int year;
    unsigned month;
    unsigned day;
  };

  TestDate dates[] = {
      {1970, 1, 1}, {2000, 1, 1}, {2000, 12, 31}, {2020, 6, 15}, {2024, 2, 29},
      {2024, 3, 1}, {2025, 7, 4}, {2026, 11, 30}, {2030, 12, 25}, {1999, 2, 28},
  };

  for (const auto& d : dates) {
    uint32_t ordinal = TimeUtils::getDayOrdinalForDate(d.year, d.month, d.day);
    int year;
    unsigned month, day;
    TEST_ASSERT_TRUE(TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day));
    TEST_ASSERT_EQUAL(d.year, year);
    TEST_ASSERT_EQUAL(d.month, month);
    TEST_ASSERT_EQUAL(d.day, day);
  }
}

// ── formatDateParts ─────────────────────────────────────────────────

void test_format_date_parts_dd_mm_yyyy() {
  // Default dateFormat is DATE_DD_MM_YYYY (0)
  SETTINGS.dateFormat = CrossPointSettings::DATE_DD_MM_YYYY;
  std::string result = TimeUtils::formatDateParts(2026, 4, 3);
  TEST_ASSERT_EQUAL_STRING("03/04/2026", result.c_str());
}

void test_format_date_parts_mm_dd_yyyy() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_MM_DD_YYYY;
  std::string result = TimeUtils::formatDateParts(2026, 4, 3);
  TEST_ASSERT_EQUAL_STRING("04/03/2026", result.c_str());
}

void test_format_date_parts_yyyy_mm_dd() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_YYYY_MM_DD;
  std::string result = TimeUtils::formatDateParts(2026, 4, 3);
  TEST_ASSERT_EQUAL_STRING("2026-04-03", result.c_str());
}

void test_format_date_parts_with_bang() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_YYYY_MM_DD;
  std::string result = TimeUtils::formatDateParts(2026, 4, 3, true);
  TEST_ASSERT_EQUAL_STRING("2026-04-03!", result.c_str());
}

void test_format_date_parts_without_bang() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_YYYY_MM_DD;
  std::string result = TimeUtils::formatDateParts(2026, 4, 3, false);
  TEST_ASSERT_EQUAL_STRING("2026-04-03", result.c_str());
}

void test_format_date_parts_single_digit_day_month() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_DD_MM_YYYY;
  std::string result = TimeUtils::formatDateParts(2026, 1, 5);
  TEST_ASSERT_EQUAL_STRING("05/01/2026", result.c_str());
}

// ── formatMonthYear ─────────────────────────────────────────────────

void test_format_month_year_iso() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_YYYY_MM_DD;
  std::string result = TimeUtils::formatMonthYear(2026, 4);
  TEST_ASSERT_EQUAL_STRING("2026-04", result.c_str());
}

void test_format_month_year_slash() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_DD_MM_YYYY;
  std::string result = TimeUtils::formatMonthYear(2026, 4);
  TEST_ASSERT_EQUAL_STRING("04/2026", result.c_str());
}

void test_format_month_year_us_format() {
  SETTINGS.dateFormat = CrossPointSettings::DATE_MM_DD_YYYY;
  std::string result = TimeUtils::formatMonthYear(2026, 12);
  TEST_ASSERT_EQUAL_STRING("12/2026", result.c_str());
}

// ── wasTimeSyncedThisBoot ───────────────────────────────────────────

void test_was_time_synced_this_boot_initially_false() {
  // Should be false since we haven't called syncTimeWithNtp
  TEST_ASSERT_FALSE(TimeUtils::wasTimeSyncedThisBoot());
}

void setUp() {
  // Reset dateFormat to default before each test
  SETTINGS.dateFormat = CrossPointSettings::DATE_DD_MM_YYYY;
}

void tearDown() {}

int main() {
  UNITY_BEGIN();

  // isClockValid
  RUN_TEST(test_clock_valid_recent_timestamp);
  RUN_TEST(test_clock_valid_exactly_at_threshold);
  RUN_TEST(test_clock_invalid_zero);
  RUN_TEST(test_clock_invalid_old_timestamp);
  RUN_TEST(test_clock_invalid_just_before_threshold);
  RUN_TEST(test_clock_valid_far_future);

  // getDayOrdinalForDate / getDateFromDayOrdinal
  RUN_TEST(test_day_ordinal_unix_epoch);
  RUN_TEST(test_day_ordinal_round_trip_2024_01_01);
  RUN_TEST(test_day_ordinal_round_trip_2026_04_03);
  RUN_TEST(test_day_ordinal_leap_year_feb29);
  RUN_TEST(test_day_ordinal_leap_year_mar1_after_feb29);
  RUN_TEST(test_day_ordinal_non_leap_year_feb28_to_mar1);
  RUN_TEST(test_day_ordinal_consecutive_days);
  RUN_TEST(test_day_ordinal_year_boundary);
  RUN_TEST(test_day_ordinal_month_lengths);
  RUN_TEST(test_day_ordinal_full_year_365);
  RUN_TEST(test_day_ordinal_leap_year_366);
  RUN_TEST(test_day_ordinal_century_leap_rules);
  RUN_TEST(test_day_ordinal_round_trip_many_dates);

  // formatDateParts
  RUN_TEST(test_format_date_parts_dd_mm_yyyy);
  RUN_TEST(test_format_date_parts_mm_dd_yyyy);
  RUN_TEST(test_format_date_parts_yyyy_mm_dd);
  RUN_TEST(test_format_date_parts_with_bang);
  RUN_TEST(test_format_date_parts_without_bang);
  RUN_TEST(test_format_date_parts_single_digit_day_month);

  // formatMonthYear
  RUN_TEST(test_format_month_year_iso);
  RUN_TEST(test_format_month_year_slash);
  RUN_TEST(test_format_month_year_us_format);

  // wasTimeSyncedThisBoot
  RUN_TEST(test_was_time_synced_this_boot_initially_false);

  return UNITY_END();
}
