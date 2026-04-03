/**
 * @file test_timezone.cpp
 * @brief Unit tests for the TimeZoneRegistry module
 *
 * Tests timezone preset lookups, clamping, and data integrity.
 */

#include <unity.h>

#include <cstring>

// Include the source directly (pure logic, no ESP32 dependencies)
#include "../../src/util/TimeZoneRegistry.h"
#include "../../src/util/TimeZoneRegistry.cpp"

// ── Preset count ────────────────────────────────────────────────────

void test_preset_count_is_29() {
  TEST_ASSERT_EQUAL(29, TimeZoneRegistry::getPresetCount());
}

void test_preset_count_is_nonzero() {
  TEST_ASSERT_GREATER_THAN(0, TimeZoneRegistry::getPresetCount());
}

// ── clampPresetIndex ────────────────────────────────────────────────

void test_clamp_valid_index_zero() {
  TEST_ASSERT_EQUAL(0, TimeZoneRegistry::clampPresetIndex(0));
}

void test_clamp_valid_index_middle() {
  TEST_ASSERT_EQUAL(14, TimeZoneRegistry::clampPresetIndex(14));
}

void test_clamp_valid_index_last() {
  uint8_t lastIndex = static_cast<uint8_t>(TimeZoneRegistry::getPresetCount() - 1);
  TEST_ASSERT_EQUAL(lastIndex, TimeZoneRegistry::clampPresetIndex(lastIndex));
}

void test_clamp_out_of_range_returns_default() {
  uint8_t outOfRange = static_cast<uint8_t>(TimeZoneRegistry::getPresetCount());
  TEST_ASSERT_EQUAL(TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX,
                    TimeZoneRegistry::clampPresetIndex(outOfRange));
}

void test_clamp_max_uint8_returns_default() {
  TEST_ASSERT_EQUAL(TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX,
                    TimeZoneRegistry::clampPresetIndex(255));
}

// ── getPresetLabel ──────────────────────────────────────────────────

void test_first_preset_label_not_null() {
  const char* label = TimeZoneRegistry::getPresetLabel(0);
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_GREATER_THAN(0, strlen(label));
}

void test_utc_preset_exists() {
  // UTC should be at index 1
  const char* label = TimeZoneRegistry::getPresetLabel(1);
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_TRUE(strstr(label, "UTC") != nullptr);
}

void test_all_presets_have_labels() {
  for (uint8_t i = 0; i < TimeZoneRegistry::getPresetCount(); i++) {
    const char* label = TimeZoneRegistry::getPresetLabel(i);
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_GREATER_THAN(0, strlen(label));
  }
}

void test_out_of_range_label_returns_default() {
  const char* defaultLabel = TimeZoneRegistry::getPresetLabel(0);
  const char* outOfRangeLabel = TimeZoneRegistry::getPresetLabel(255);
  TEST_ASSERT_EQUAL_STRING(defaultLabel, outOfRangeLabel);
}

// ── getPresetPosixTz ────────────────────────────────────────────────

void test_utc_posix_tz() {
  const char* tz = TimeZoneRegistry::getPresetPosixTz(1);
  TEST_ASSERT_EQUAL_STRING("UTC0", tz);
}

void test_all_presets_have_posix_tz() {
  for (uint8_t i = 0; i < TimeZoneRegistry::getPresetCount(); i++) {
    const char* tz = TimeZoneRegistry::getPresetPosixTz(i);
    TEST_ASSERT_NOT_NULL(tz);
    TEST_ASSERT_GREATER_THAN(0, strlen(tz));
  }
}

void test_new_york_timezone() {
  // New York should be at index 9
  const char* label = TimeZoneRegistry::getPresetLabel(9);
  TEST_ASSERT_TRUE(strstr(label, "New York") != nullptr);
  const char* tz = TimeZoneRegistry::getPresetPosixTz(9);
  TEST_ASSERT_TRUE(strstr(tz, "EST5EDT") != nullptr);
}

void test_tokyo_timezone() {
  // Tokyo should be at index 25
  const char* label = TimeZoneRegistry::getPresetLabel(25);
  TEST_ASSERT_TRUE(strstr(label, "Tokyo") != nullptr);
  const char* tz = TimeZoneRegistry::getPresetPosixTz(25);
  TEST_ASSERT_TRUE(strstr(tz, "JST-9") != nullptr);
}

// ── getPreset ───────────────────────────────────────────────────────

void test_get_preset_returns_valid_struct() {
  const TimeZonePreset& preset = TimeZoneRegistry::getPreset(0);
  TEST_ASSERT_NOT_NULL(preset.label);
  TEST_ASSERT_NOT_NULL(preset.posixTz);
}

void test_get_preset_out_of_range_returns_default() {
  const TimeZonePreset& defaultPreset = TimeZoneRegistry::getPreset(0);
  const TimeZonePreset& outOfRange = TimeZoneRegistry::getPreset(255);
  TEST_ASSERT_EQUAL_STRING(defaultPreset.label, outOfRange.label);
  TEST_ASSERT_EQUAL_STRING(defaultPreset.posixTz, outOfRange.posixTz);
}

// ── DEFAULT_TIME_ZONE_INDEX ─────────────────────────────────────────

void test_default_timezone_index_is_zero() {
  TEST_ASSERT_EQUAL(0, TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

  // Preset count
  RUN_TEST(test_preset_count_is_29);
  RUN_TEST(test_preset_count_is_nonzero);

  // clampPresetIndex
  RUN_TEST(test_clamp_valid_index_zero);
  RUN_TEST(test_clamp_valid_index_middle);
  RUN_TEST(test_clamp_valid_index_last);
  RUN_TEST(test_clamp_out_of_range_returns_default);
  RUN_TEST(test_clamp_max_uint8_returns_default);

  // getPresetLabel
  RUN_TEST(test_first_preset_label_not_null);
  RUN_TEST(test_utc_preset_exists);
  RUN_TEST(test_all_presets_have_labels);
  RUN_TEST(test_out_of_range_label_returns_default);

  // getPresetPosixTz
  RUN_TEST(test_utc_posix_tz);
  RUN_TEST(test_all_presets_have_posix_tz);
  RUN_TEST(test_new_york_timezone);
  RUN_TEST(test_tokyo_timezone);

  // getPreset
  RUN_TEST(test_get_preset_returns_valid_struct);
  RUN_TEST(test_get_preset_out_of_range_returns_default);

  // Default index
  RUN_TEST(test_default_timezone_index_is_zero);

  return UNITY_END();
}
