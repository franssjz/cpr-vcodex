#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace LyraCustomMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverTileHeight = 336;
  v.homeRecentBooksCount = 3;
  v.keyboardKeyHeight = 50;
  v.keyboardCenteredText = true;
  return v;
}();
}

class LyraCustomTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
