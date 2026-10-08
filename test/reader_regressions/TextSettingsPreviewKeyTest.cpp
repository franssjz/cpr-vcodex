#include <gtest/gtest.h>

#include "activities/settings/TextSettingsPreview.h"

TEST(TextSettingsPreviewKey, DistinguishesAllBionicModes) {
  for (uint8_t mode = 0; mode < 3; ++mode) {
    for (uint8_t other = 0; other < 3; ++other) {
      textsettings::PreviewKey a, b;
      a.bionicReading = mode;
      b.bionicReading = other;
      EXPECT_EQ(a == b, mode == other);
    }
  }
}

TEST(TextSettingsPreviewKey, ForcedIndentInvalidatesParagraphLayout) {
  textsettings::PreviewKey a, b;
  a.extraParagraphSpacing = b.extraParagraphSpacing = true;
  b.forceParagraphIndents = true;
  EXPECT_NE(a, b);
  a.forceParagraphIndents = true;
  EXPECT_EQ(a, b);
}
