/**
 * @file test_settings_helpers.cpp
 * @brief Unit tests for CrossPointSettings helper methods
 *
 * Tests getDailyGoalMs, getSleepTimeoutMs, getRefreshFrequency,
 * getReaderLineCompression, getPowerButtonDuration, and
 * validateFrontButtonMapping.
 */

#include <unity.h>

#include <cstdint>

// Include the source under test (mocks provide ESP32 stubs)
#include "../../src/CrossPointSettings.cpp"

// ── getDailyGoalMs ─────────────────────────────────────────────────

void test_daily_goal_15_min() {
  SETTINGS.dailyGoalTarget = CrossPointSettings::DAILY_GOAL_15_MIN;
  TEST_ASSERT_EQUAL_UINT64(15ULL * 60 * 1000, SETTINGS.getDailyGoalMs());
}

void test_daily_goal_30_min() {
  SETTINGS.dailyGoalTarget = CrossPointSettings::DAILY_GOAL_30_MIN;
  TEST_ASSERT_EQUAL_UINT64(30ULL * 60 * 1000, SETTINGS.getDailyGoalMs());
}

void test_daily_goal_45_min() {
  SETTINGS.dailyGoalTarget = CrossPointSettings::DAILY_GOAL_45_MIN;
  TEST_ASSERT_EQUAL_UINT64(45ULL * 60 * 1000, SETTINGS.getDailyGoalMs());
}

void test_daily_goal_60_min() {
  SETTINGS.dailyGoalTarget = CrossPointSettings::DAILY_GOAL_60_MIN;
  TEST_ASSERT_EQUAL_UINT64(60ULL * 60 * 1000, SETTINGS.getDailyGoalMs());
}

void test_daily_goal_default_is_30_min() {
  // Invalid value should default to 30 minutes
  SETTINGS.dailyGoalTarget = 255;
  TEST_ASSERT_EQUAL_UINT64(30ULL * 60 * 1000, SETTINGS.getDailyGoalMs());
}

// ── getSleepTimeoutMs ───────────────────────────────────────────────

void test_sleep_timeout_1_min() {
  SETTINGS.sleepTimeout = CrossPointSettings::SLEEP_1_MIN;
  TEST_ASSERT_EQUAL(1UL * 60 * 1000, SETTINGS.getSleepTimeoutMs());
}

void test_sleep_timeout_5_min() {
  SETTINGS.sleepTimeout = CrossPointSettings::SLEEP_5_MIN;
  TEST_ASSERT_EQUAL(5UL * 60 * 1000, SETTINGS.getSleepTimeoutMs());
}

void test_sleep_timeout_10_min() {
  SETTINGS.sleepTimeout = CrossPointSettings::SLEEP_10_MIN;
  TEST_ASSERT_EQUAL(10UL * 60 * 1000, SETTINGS.getSleepTimeoutMs());
}

void test_sleep_timeout_15_min() {
  SETTINGS.sleepTimeout = CrossPointSettings::SLEEP_15_MIN;
  TEST_ASSERT_EQUAL(15UL * 60 * 1000, SETTINGS.getSleepTimeoutMs());
}

void test_sleep_timeout_30_min() {
  SETTINGS.sleepTimeout = CrossPointSettings::SLEEP_30_MIN;
  TEST_ASSERT_EQUAL(30UL * 60 * 1000, SETTINGS.getSleepTimeoutMs());
}

void test_sleep_timeout_default_is_10_min() {
  SETTINGS.sleepTimeout = 255;
  TEST_ASSERT_EQUAL(10UL * 60 * 1000, SETTINGS.getSleepTimeoutMs());
}

// ── getRefreshFrequency ─────────────────────────────────────────────

void test_refresh_frequency_1() {
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_1;
  TEST_ASSERT_EQUAL(1, SETTINGS.getRefreshFrequency());
}

void test_refresh_frequency_5() {
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_5;
  TEST_ASSERT_EQUAL(5, SETTINGS.getRefreshFrequency());
}

void test_refresh_frequency_10() {
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_10;
  TEST_ASSERT_EQUAL(10, SETTINGS.getRefreshFrequency());
}

void test_refresh_frequency_15() {
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_15;
  TEST_ASSERT_EQUAL(15, SETTINGS.getRefreshFrequency());
}

void test_refresh_frequency_30() {
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_30;
  TEST_ASSERT_EQUAL(30, SETTINGS.getRefreshFrequency());
}

void test_refresh_frequency_default_is_15() {
  SETTINGS.refreshFrequency = 255;
  TEST_ASSERT_EQUAL(15, SETTINGS.getRefreshFrequency());
}

// ── getReaderLineCompression ────────────────────────────────────────

void test_line_compression_bookerly_tight() {
  SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
  SETTINGS.lineSpacing = CrossPointSettings::TIGHT;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_bookerly_normal() {
  SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
  SETTINGS.lineSpacing = CrossPointSettings::NORMAL;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_bookerly_wide() {
  SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
  SETTINGS.lineSpacing = CrossPointSettings::WIDE;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.1f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_notosans_tight() {
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.lineSpacing = CrossPointSettings::TIGHT;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.90f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_notosans_normal() {
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.lineSpacing = CrossPointSettings::NORMAL;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_notosans_wide() {
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.lineSpacing = CrossPointSettings::WIDE;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_opendyslexic_tight() {
  SETTINGS.fontFamily = CrossPointSettings::OPENDYSLEXIC;
  SETTINGS.lineSpacing = CrossPointSettings::TIGHT;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.90f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_opendyslexic_normal() {
  SETTINGS.fontFamily = CrossPointSettings::OPENDYSLEXIC;
  SETTINGS.lineSpacing = CrossPointSettings::NORMAL;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, SETTINGS.getReaderLineCompression());
}

void test_line_compression_opendyslexic_wide() {
  SETTINGS.fontFamily = CrossPointSettings::OPENDYSLEXIC;
  SETTINGS.lineSpacing = CrossPointSettings::WIDE;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, SETTINGS.getReaderLineCompression());
}

// ── getPowerButtonDuration ──────────────────────────────────────────

void test_power_button_duration_sleep() {
  SETTINGS.shortPwrBtn = CrossPointSettings::SLEEP;
  TEST_ASSERT_EQUAL(10, SETTINGS.getPowerButtonDuration());
}

void test_power_button_duration_ignore() {
  SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
  TEST_ASSERT_EQUAL(400, SETTINGS.getPowerButtonDuration());
}

void test_power_button_duration_page_turn() {
  SETTINGS.shortPwrBtn = CrossPointSettings::PAGE_TURN;
  TEST_ASSERT_EQUAL(400, SETTINGS.getPowerButtonDuration());
}

// ── validateFrontButtonMapping ──────────────────────────────────────

void test_validate_mapping_valid_default() {
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  CrossPointSettings::validateFrontButtonMapping(SETTINGS);
  // Should remain unchanged
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_BACK, SETTINGS.frontButtonBack);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_CONFIRM, SETTINGS.frontButtonConfirm);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_LEFT, SETTINGS.frontButtonLeft);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_RIGHT, SETTINGS.frontButtonRight);
}

void test_validate_mapping_valid_swapped() {
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
  CrossPointSettings::validateFrontButtonMapping(SETTINGS);
  // Valid permutation should remain unchanged
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_LEFT, SETTINGS.frontButtonBack);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_RIGHT, SETTINGS.frontButtonConfirm);
}

void test_validate_mapping_duplicate_resets_to_default() {
  // Set duplicate mapping (two buttons mapped to same hardware)
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_BACK;  // duplicate!
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  CrossPointSettings::validateFrontButtonMapping(SETTINGS);
  // Should reset to default
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_BACK, SETTINGS.frontButtonBack);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_CONFIRM, SETTINGS.frontButtonConfirm);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_LEFT, SETTINGS.frontButtonLeft);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_RIGHT, SETTINGS.frontButtonRight);
}

void test_validate_mapping_all_same_resets() {
  SETTINGS.frontButtonBack = 0;
  SETTINGS.frontButtonConfirm = 0;
  SETTINGS.frontButtonLeft = 0;
  SETTINGS.frontButtonRight = 0;
  CrossPointSettings::validateFrontButtonMapping(SETTINGS);
  // Should reset to default
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_BACK, SETTINGS.frontButtonBack);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_CONFIRM, SETTINGS.frontButtonConfirm);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_LEFT, SETTINGS.frontButtonLeft);
  TEST_ASSERT_EQUAL(CrossPointSettings::FRONT_HW_RIGHT, SETTINGS.frontButtonRight);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

  // getDailyGoalMs
  RUN_TEST(test_daily_goal_15_min);
  RUN_TEST(test_daily_goal_30_min);
  RUN_TEST(test_daily_goal_45_min);
  RUN_TEST(test_daily_goal_60_min);
  RUN_TEST(test_daily_goal_default_is_30_min);

  // getSleepTimeoutMs
  RUN_TEST(test_sleep_timeout_1_min);
  RUN_TEST(test_sleep_timeout_5_min);
  RUN_TEST(test_sleep_timeout_10_min);
  RUN_TEST(test_sleep_timeout_15_min);
  RUN_TEST(test_sleep_timeout_30_min);
  RUN_TEST(test_sleep_timeout_default_is_10_min);

  // getRefreshFrequency
  RUN_TEST(test_refresh_frequency_1);
  RUN_TEST(test_refresh_frequency_5);
  RUN_TEST(test_refresh_frequency_10);
  RUN_TEST(test_refresh_frequency_15);
  RUN_TEST(test_refresh_frequency_30);
  RUN_TEST(test_refresh_frequency_default_is_15);

  // getReaderLineCompression
  RUN_TEST(test_line_compression_bookerly_tight);
  RUN_TEST(test_line_compression_bookerly_normal);
  RUN_TEST(test_line_compression_bookerly_wide);
  RUN_TEST(test_line_compression_notosans_tight);
  RUN_TEST(test_line_compression_notosans_normal);
  RUN_TEST(test_line_compression_notosans_wide);
  RUN_TEST(test_line_compression_opendyslexic_tight);
  RUN_TEST(test_line_compression_opendyslexic_normal);
  RUN_TEST(test_line_compression_opendyslexic_wide);

  // getPowerButtonDuration
  RUN_TEST(test_power_button_duration_sleep);
  RUN_TEST(test_power_button_duration_ignore);
  RUN_TEST(test_power_button_duration_page_turn);

  // validateFrontButtonMapping
  RUN_TEST(test_validate_mapping_valid_default);
  RUN_TEST(test_validate_mapping_valid_swapped);
  RUN_TEST(test_validate_mapping_duplicate_resets_to_default);
  RUN_TEST(test_validate_mapping_all_same_resets);

  return UNITY_END();
}
