/**
 * @file test_utf8.cpp
 * @brief Comprehensive unit tests for the Utf8 library
 *
 * Tests UTF-8 codepoint parsing, string manipulation, boundary truncation,
 * and combining mark detection.
 */

#include <unity.h>

#include <cstring>
#include <string>

#include "Utf8.h"

// ── utf8NextCodepoint ────────────────────────────────────────────────

void test_ascii_single_char() {
  const unsigned char str[] = "A";
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32('A', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_PTR(str + 1, p);
}

void test_ascii_string() {
  const unsigned char str[] = "Hello";
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32('H', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_UINT32('e', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_UINT32('l', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_UINT32('l', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_UINT32('o', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&p));  // null terminator
}

void test_null_terminator() {
  const unsigned char str[] = "";
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&p));
}

void test_two_byte_sequence() {
  // 'ñ' = U+00F1 = 0xC3 0xB1
  const unsigned char str[] = {0xC3, 0xB1, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(0x00F1, utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_PTR(str + 2, p);
}

void test_three_byte_sequence() {
  // '€' = U+20AC = 0xE2 0x82 0xAC
  const unsigned char str[] = {0xE2, 0x82, 0xAC, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(0x20AC, utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_PTR(str + 3, p);
}

void test_four_byte_sequence() {
  // '𝄞' (Musical Symbol G Clef) = U+1D11E = 0xF0 0x9D 0x84 0x9E
  const unsigned char str[] = {0xF0, 0x9D, 0x84, 0x9E, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(0x1D11E, utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_PTR(str + 4, p);
}

void test_mixed_multibyte() {
  // "Aé€" = A (1 byte) + é (2 bytes) + € (3 bytes)
  const unsigned char str[] = {'A', 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32('A', utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_UINT32(0x00E9, utf8NextCodepoint(&p));  // é
  TEST_ASSERT_EQUAL_UINT32(0x20AC, utf8NextCodepoint(&p));  // €
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&p));
}

void test_invalid_continuation_byte_standalone() {
  // Stray continuation byte 0x80 should produce REPLACEMENT_GLYPH
  const unsigned char str[] = {0x80, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
  TEST_ASSERT_EQUAL_PTR(str + 1, p);
}

void test_invalid_fe_byte() {
  // 0xFE is not a valid UTF-8 lead byte
  const unsigned char str[] = {0xFE, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
}

void test_invalid_ff_byte() {
  // 0xFF is not a valid UTF-8 lead byte
  const unsigned char str[] = {0xFF, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
}

void test_overlong_encoding_two_byte() {
  // Overlong encoding of NUL (U+0000): 0xC0 0x80 (should be rejected)
  const unsigned char str[] = {0xC0, 0x80, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
}

void test_overlong_encoding_three_byte() {
  // Overlong 3-byte encoding of '/' (U+002F): 0xE0 0x80 0xAF
  const unsigned char str[] = {0xE0, 0x80, 0xAF, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
}

void test_surrogate_half_rejected() {
  // U+D800 (high surrogate) encoded as 0xED 0xA0 0x80 — must be rejected
  const unsigned char str[] = {0xED, 0xA0, 0x80, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
}

void test_truncated_two_byte_sequence() {
  // Lead byte for 2-byte sequence but missing continuation byte
  const unsigned char str[] = {0xC3, 0};
  const unsigned char* p = str;
  uint32_t cp = utf8NextCodepoint(&p);
  // Should produce REPLACEMENT_GLYPH or handle gracefully
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, cp);
}

void test_truncated_three_byte_sequence() {
  // Lead byte for 3-byte sequence with only 1 continuation
  const unsigned char str[] = {0xE2, 0x82, 0};
  const unsigned char* p = str;
  uint32_t cp = utf8NextCodepoint(&p);
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, cp);
}

void test_max_unicode_codepoint() {
  // U+10FFFF (max valid) = 0xF4 0x8F 0xBF 0xBF
  const unsigned char str[] = {0xF4, 0x8F, 0xBF, 0xBF, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(0x10FFFF, utf8NextCodepoint(&p));
}

void test_beyond_max_unicode_rejected() {
  // U+110000 (just beyond max) = 0xF4 0x90 0x80 0x80 — must be rejected
  const unsigned char str[] = {0xF4, 0x90, 0x80, 0x80, 0};
  const unsigned char* p = str;
  TEST_ASSERT_EQUAL_UINT32(REPLACEMENT_GLYPH, utf8NextCodepoint(&p));
}

// ── utf8RemoveLastChar ──────────────────────────────────────────────

void test_remove_last_ascii() {
  std::string s = "Hello";
  size_t newSize = utf8RemoveLastChar(s);
  TEST_ASSERT_EQUAL_STRING("Hell", s.c_str());
  TEST_ASSERT_EQUAL(4, newSize);
}

void test_remove_last_two_byte() {
  // "Aé" → "A"
  std::string s = "A\xC3\xA9";
  utf8RemoveLastChar(s);
  TEST_ASSERT_EQUAL_STRING("A", s.c_str());
}

void test_remove_last_three_byte() {
  // "A€" → "A"
  std::string s = "A\xE2\x82\xAC";
  utf8RemoveLastChar(s);
  TEST_ASSERT_EQUAL_STRING("A", s.c_str());
}

void test_remove_last_four_byte() {
  // "A𝄞" → "A"
  std::string s = std::string("A") + std::string("\xF0\x9D\x84\x9E");
  utf8RemoveLastChar(s);
  TEST_ASSERT_EQUAL_STRING("A", s.c_str());
}

void test_remove_last_single_char() {
  std::string s = "X";
  utf8RemoveLastChar(s);
  TEST_ASSERT_TRUE(s.empty());
}

void test_remove_last_empty_string() {
  std::string s = "";
  size_t result = utf8RemoveLastChar(s);
  TEST_ASSERT_EQUAL(0, result);
  TEST_ASSERT_TRUE(s.empty());
}

// ── utf8TruncateChars ───────────────────────────────────────────────

void test_truncate_zero_chars() {
  std::string s = "Hello";
  utf8TruncateChars(s, 0);
  TEST_ASSERT_EQUAL_STRING("Hello", s.c_str());
}

void test_truncate_one_ascii_char() {
  std::string s = "Hello";
  utf8TruncateChars(s, 1);
  TEST_ASSERT_EQUAL_STRING("Hell", s.c_str());
}

void test_truncate_all_chars() {
  std::string s = "Hi";
  utf8TruncateChars(s, 2);
  TEST_ASSERT_TRUE(s.empty());
}

void test_truncate_more_than_length() {
  std::string s = "AB";
  utf8TruncateChars(s, 10);
  TEST_ASSERT_TRUE(s.empty());
}

void test_truncate_multibyte_chars() {
  // "Aé€" (A=1byte, é=2bytes, €=3bytes) → remove 2 chars → "A"
  std::string s = "A\xC3\xA9\xE2\x82\xAC";
  utf8TruncateChars(s, 2);
  TEST_ASSERT_EQUAL_STRING("A", s.c_str());
}

void test_truncate_empty_string() {
  std::string s = "";
  utf8TruncateChars(s, 5);
  TEST_ASSERT_TRUE(s.empty());
}

// ── utf8SafeTruncateBuffer ──────────────────────────────────────────

void test_safe_truncate_complete_ascii() {
  const char* buf = "Hello";
  TEST_ASSERT_EQUAL(5, utf8SafeTruncateBuffer(buf, 5));
}

void test_safe_truncate_complete_multibyte() {
  // "é" = 0xC3 0xA9 (2 bytes, complete)
  const char buf[] = {(char)0xC3, (char)0xA9};
  TEST_ASSERT_EQUAL(2, utf8SafeTruncateBuffer(buf, 2));
}

void test_safe_truncate_incomplete_two_byte() {
  // "Aé" but truncated at byte 2: 'A' (complete) + 0xC3 (incomplete 2-byte start)
  const char buf[] = {'A', (char)0xC3};
  TEST_ASSERT_EQUAL(1, utf8SafeTruncateBuffer(buf, 2));
}

void test_safe_truncate_incomplete_three_byte() {
  // "A€" but truncated in the middle of €: 'A' + 0xE2 0x82 (incomplete 3-byte)
  const char buf[] = {'A', (char)0xE2, (char)0x82};
  TEST_ASSERT_EQUAL(1, utf8SafeTruncateBuffer(buf, 3));
}

void test_safe_truncate_empty() { TEST_ASSERT_EQUAL(0, utf8SafeTruncateBuffer("", 0)); }

void test_safe_truncate_negative_length() { TEST_ASSERT_EQUAL(0, utf8SafeTruncateBuffer("Hello", -1)); }

void test_safe_truncate_single_complete_char() {
  const char buf[] = {(char)0xE2, (char)0x82, (char)0xAC};  // €
  TEST_ASSERT_EQUAL(3, utf8SafeTruncateBuffer(buf, 3));
}

// ── utf8IsCombiningMark ─────────────────────────────────────────────

void test_combining_mark_basic_diacritical() {
  // U+0300 = Combining Grave Accent (start of range)
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x0300));
  // U+036F = end of Combining Diacritical Marks range
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x036F));
  // U+0301 = Combining Acute Accent
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x0301));
}

void test_combining_mark_supplement() {
  // U+1DC0 = start of Combining Diacritical Marks Supplement
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x1DC0));
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x1DFF));
}

void test_combining_mark_for_symbols() {
  // U+20D0 = start of Combining Diacritical Marks for Symbols
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x20D0));
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0x20FF));
}

void test_combining_mark_half_marks() {
  // U+FE20 = start of Combining Half Marks
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0xFE20));
  TEST_ASSERT_TRUE(utf8IsCombiningMark(0xFE2F));
}

void test_non_combining_ascii() {
  TEST_ASSERT_FALSE(utf8IsCombiningMark('A'));
  TEST_ASSERT_FALSE(utf8IsCombiningMark(' '));
  TEST_ASSERT_FALSE(utf8IsCombiningMark('0'));
}

void test_non_combining_outside_ranges() {
  // Just outside the combining ranges
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x02FF));  // just before 0x0300
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x0370));  // just after 0x036F
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x1DBF));  // just before 0x1DC0
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x1E00));  // just after 0x1DFF
}

void test_non_combining_common_unicode() {
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x00E9));   // é (precomposed)
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x20AC));   // €
  TEST_ASSERT_FALSE(utf8IsCombiningMark(0x1D11E));  // 𝄞
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

  // utf8NextCodepoint
  RUN_TEST(test_ascii_single_char);
  RUN_TEST(test_ascii_string);
  RUN_TEST(test_null_terminator);
  RUN_TEST(test_two_byte_sequence);
  RUN_TEST(test_three_byte_sequence);
  RUN_TEST(test_four_byte_sequence);
  RUN_TEST(test_mixed_multibyte);
  RUN_TEST(test_invalid_continuation_byte_standalone);
  RUN_TEST(test_invalid_fe_byte);
  RUN_TEST(test_invalid_ff_byte);
  RUN_TEST(test_overlong_encoding_two_byte);
  RUN_TEST(test_overlong_encoding_three_byte);
  RUN_TEST(test_surrogate_half_rejected);
  RUN_TEST(test_truncated_two_byte_sequence);
  RUN_TEST(test_truncated_three_byte_sequence);
  RUN_TEST(test_max_unicode_codepoint);
  RUN_TEST(test_beyond_max_unicode_rejected);

  // utf8RemoveLastChar
  RUN_TEST(test_remove_last_ascii);
  RUN_TEST(test_remove_last_two_byte);
  RUN_TEST(test_remove_last_three_byte);
  RUN_TEST(test_remove_last_four_byte);
  RUN_TEST(test_remove_last_single_char);
  RUN_TEST(test_remove_last_empty_string);

  // utf8TruncateChars
  RUN_TEST(test_truncate_zero_chars);
  RUN_TEST(test_truncate_one_ascii_char);
  RUN_TEST(test_truncate_all_chars);
  RUN_TEST(test_truncate_more_than_length);
  RUN_TEST(test_truncate_multibyte_chars);
  RUN_TEST(test_truncate_empty_string);

  // utf8SafeTruncateBuffer
  RUN_TEST(test_safe_truncate_complete_ascii);
  RUN_TEST(test_safe_truncate_complete_multibyte);
  RUN_TEST(test_safe_truncate_incomplete_two_byte);
  RUN_TEST(test_safe_truncate_incomplete_three_byte);
  RUN_TEST(test_safe_truncate_empty);
  RUN_TEST(test_safe_truncate_negative_length);
  RUN_TEST(test_safe_truncate_single_complete_char);

  // utf8IsCombiningMark
  RUN_TEST(test_combining_mark_basic_diacritical);
  RUN_TEST(test_combining_mark_supplement);
  RUN_TEST(test_combining_mark_for_symbols);
  RUN_TEST(test_combining_mark_half_marks);
  RUN_TEST(test_non_combining_ascii);
  RUN_TEST(test_non_combining_outside_ranges);
  RUN_TEST(test_non_combining_common_unicode);

  return UNITY_END();
}
