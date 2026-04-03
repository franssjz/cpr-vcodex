#pragma once

#include <string>

class GfxRenderer;

namespace HeaderDateUtils {

struct DisplayDateInfo {
  uint32_t timestamp = 0;
  bool usedFallback = false;
};

DisplayDateInfo getDisplayDateInfo();
std::string getDisplayDateText();
void drawHeaderWithDate(const GfxRenderer& renderer, const char* title, const char* subtitle = nullptr);

}  // namespace HeaderDateUtils
