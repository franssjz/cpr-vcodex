#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "EpdFont.h"

struct FontCase {
  const char* name;
  const EpdFontData* data;
  uint32_t denseHash;
};

#include "builtin_kerning_cases.generated.h"

TEST(BuiltinKerningRuntime, EveryClassPairMatchesOriginalDenseTables) {
  for (const auto& fontCase : kFonts) {
    SCOPED_TRACE(fontCase.name);
    const auto& data = *fontCase.data;
    ASSERT_EQ(data.kernMatrix, nullptr);
    ASSERT_NE(data.kernRowOffsets, nullptr);
    std::array<uint16_t, 256> left{}, right{};
    for (unsigned i = 0; i < data.kernLeftEntryCount; ++i) {
      left[data.kernLeftClasses[i].classId] = data.kernLeftClasses[i].codepoint;
    }
    for (unsigned i = 0; i < data.kernRightEntryCount; ++i) {
      right[data.kernRightClasses[i].classId] = data.kernRightClasses[i].codepoint;
    }
    EpdFont font(&data);
    uint32_t hash = 2166136261u;
    for (unsigned l = 1; l <= data.kernLeftClassCount; ++l) {
      ASSERT_NE(left[l], 0);
      for (unsigned r = 1; r <= data.kernRightClassCount; ++r) {
        ASSERT_NE(right[r], 0);
        hash = (hash ^ static_cast<uint8_t>(font.getKerning(left[l], right[r]))) * 16777619u;
      }
    }
    EXPECT_EQ(hash, fontCase.denseHash);
    EXPECT_EQ(font.getKerning(0, right[1]), 0);
    EXPECT_EQ(font.getKerning(left[1], 0), 0);
  }
}
