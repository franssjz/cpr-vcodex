#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace LyraVcodex2Metrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.contentSidePadding = 16;
  v.verticalSpacing = 12;
  v.menuRowHeight = 58;
  v.menuSpacing = 8;
  v.homeTopPadding = 48;
  v.homeCoverHeight = 150;
  v.homeCoverTileHeight = 348;
  v.homeRecentBooksCount = 2;
  v.keyboardKeyHeight = 50;
  v.keyboardCenteredText = true;
  return v;
}();
}  // namespace LyraVcodex2Metrics

class LyraVcodex2Theme : public LyraTheme {
 public:
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowCompleted = nullptr) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
