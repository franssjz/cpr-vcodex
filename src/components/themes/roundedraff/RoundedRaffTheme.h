#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace RoundedRaffMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 .topPadding = 0,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 48,
                                 .verticalSpacing = 10,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 42,
                                 .listWithSubtitleRowHeight = 66,
                                 .menuRowHeight = 46,
                                 .menuSpacing = 6,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 48,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 52,
                                 .homeCoverHeight = 226,
                                 .homeCoverTileHeight = 242,
                                 .homeRecentBooksCount = 1,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 29,
                                 .keyboardKeyHeight = 34,
                                 .keyboardKeySpacing = 6,
                                 .keyboardBottomKeyHeight = 32,
                                 .keyboardBottomKeySpacing = 5,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -2,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 92};
}

class RoundedRaffTheme final : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowCompleted = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2,
                       const char* btn3, const char* btn4) const override;
  void drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, bool isSelected,
                       const char* secondaryLabel = nullptr, KeyboardKeyType keyType = KeyboardKeyType::Normal,
                       bool inactiveSelection = false) const override;
};
