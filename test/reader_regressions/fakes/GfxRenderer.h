#pragma once

#include <EpdFontFamily.h>

#include <cstring>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}  // namespace BidiUtils

class GfxRenderer {
 public:
  enum class TextMeasureMode { Layout, Rendered };
  bool isFontCacheScanning() const { return false; }
  int getFontAscenderSize(int) const { return 20; }
  int getScreenWidth() const { return 800; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style, int8_t = 0,
                      BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO,
                      TextMeasureMode = TextMeasureMode::Layout) const {
    return text ? static_cast<int>(std::strlen(text)) : 0;
  }
  int getTextWidth(int, const char* text, EpdFontFamily::Style = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {
    return text ? static_cast<int>(std::strlen(text)) : 0;
  }
  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO, int8_t = 0) const {}
  void drawLine(int, int, int, int, int, bool) const {}
};
