#include <gtest/gtest.h>

#include <cstdint>

#include "lib/Epub/Epub/converters/PixelCacheFormat.h"

TEST(ImageCacheFormatRegression, UsesVersionedEightByteHeader) {
  EXPECT_EQ(PXC_MAGIC, 0x5850);
  EXPECT_EQ(PXC_VERSION, 1);
  EXPECT_EQ(PXC_HEADER_BYTES, 8U);
}

TEST(ImageCacheFormatRegression, AccountsForPackedRowsAndHeader) {
  EXPECT_EQ(pxcExpectedSize(1, 1), 9U);
  EXPECT_EQ(pxcExpectedSize(4, 2), 10U);
  EXPECT_EQ(pxcExpectedSize(5, 3), 14U);
  EXPECT_EQ(pxcExpectedSize(482, 728), 8U + 121U * 728U);
}

TEST(ImageCacheFormatRegression, SeparatesWaveformCalibrations) {
  EXPECT_NE(static_cast<uint8_t>(PixelCacheVariant::Differential),
            static_cast<uint8_t>(PixelCacheVariant::FactoryLut));
}
