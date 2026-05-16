#include "RoundedRaffTheme.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kPillRadius = 18;
constexpr int kRowRadius = 14;

int pageStartForSelection(const int selectedIndex, const int pageItems) {
  if (selectedIndex < 0 || pageItems <= 0) {
    return 0;
  }
  return (selectedIndex / pageItems) * pageItems;
}

void drawScrollBar(const GfxRenderer& renderer, Rect rect, const int itemCount, const int pageStart,
                   const int pageItems) {
  if (itemCount <= pageItems || pageItems <= 0) {
    return;
  }
  const auto& metrics = RoundedRaffMetrics::values;
  const int barW = metrics.scrollBarWidth;
  const int barX = rect.x + rect.width - metrics.scrollBarRightOffset - barW;
  const int thumbH = std::max(10, (rect.height * pageItems) / itemCount);
  const int maxStart = std::max(1, itemCount - pageItems);
  const int maxTravel = std::max(1, rect.height - thumbH);
  const int thumbY = rect.y + (std::clamp(pageStart, 0, maxStart) * maxTravel) / maxStart;
  renderer.fillRect(barX, thumbY, barW, thumbH);
}
}  // namespace

void RoundedRaffTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                  const char* subtitle) const {
  (void)subtitle;
  if (title == nullptr) {
    return;
  }
  const auto& metrics = RoundedRaffMetrics::values;
  const int titleX = rect.x + metrics.contentSidePadding;
  const int titleY = rect.y + 14;
  const int batteryX = rect.x + rect.width - metrics.contentSidePadding - metrics.batteryWidth;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int reservedRight = showBatteryPercentage ? 62 : 28;
  const int maxTitleWidth = std::max(0, rect.width - metrics.contentSidePadding * 2 - reservedRight);
  const auto headerTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, titleX, titleY, headerTitle.c_str(), true, EpdFontFamily::BOLD);
  drawBatteryRight(renderer, Rect{batteryX, rect.y + 15, metrics.batteryWidth, metrics.batteryHeight},
                   showBatteryPercentage);
}

void RoundedRaffTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                  bool selected) const {
  if (tabs.empty()) {
    return;
  }
  const int slotWidth = rect.width / static_cast<int>(tabs.size());
  const int tabY = rect.y + 5;
  const int tabHeight = rect.height - 10;
  for (size_t i = 0; i < tabs.size(); ++i) {
    const int slotX = rect.x + static_cast<int>(i) * slotWidth;
    const int tabX = slotX + 4;
    const int tabWidth = slotWidth - 8;
    const bool active = tabs[i].selected;
    if (active) {
      renderer.fillRoundedRect(tabX, tabY, tabWidth, tabHeight, kPillRadius,
                               selected ? Color::Black : Color::LightGray);
    }
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, tabs[i].label, EpdFontFamily::BOLD);
    const int textX = slotX + (slotWidth - textW) / 2;
    const int textY = tabY + (tabHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, tabs[i].label, !(active && selected), EpdFontFamily::BOLD);
  }
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void RoundedRaffTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                const std::function<std::string(int index)>& rowTitle,
                                const std::function<std::string(int index)>& rowSubtitle,
                                const std::function<UIIcon(int index)>& rowIcon,
                                const std::function<std::string(int index)>& rowValue, bool highlightValue,
                                const std::function<bool(int index)>& rowCompleted) const {
  (void)rowIcon;
  (void)rowCompleted;
  const auto& metrics = RoundedRaffMetrics::values;
  const bool hasSubtitle = static_cast<bool>(rowSubtitle);
  const int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  const int rowGap = 6;
  const int rowStep = rowHeight + rowGap;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int pageStart = pageStartForSelection(selectedIndex, pageItems);
  const int rowX = rect.x + metrics.contentSidePadding;
  const int rowW = rect.width - metrics.contentSidePadding * 2 - metrics.scrollBarRightOffset;

  for (int i = pageStart; i < itemCount && i < pageStart + pageItems; ++i) {
    const int rowY = rect.y + (i - pageStart) * rowStep;
    const bool active = selectedIndex == i;
    if (active) {
      renderer.fillRoundedRect(rowX, rowY, rowW, rowHeight, kRowRadius, Color::Black);
    }

    const int textX = rowX + 12;
    const int valueMaxW = rowW / 2 - 14;
    std::string value = rowValue ? rowValue(i) : "";
    if (!value.empty()) {
      value = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), valueMaxW);
      const int valueW = renderer.getTextWidth(UI_10_FONT_ID, value.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rowX + rowW - valueW - 12, rowY + 11, value.c_str(), !active || !highlightValue,
                        EpdFontFamily::BOLD);
    }

    const int titleMaxW = value.empty() ? rowW - 24 : rowW - valueMaxW - 28;
    std::string title = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), titleMaxW, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, textX, rowY + 11, title.c_str(), !active, EpdFontFamily::BOLD);
    if (hasSubtitle) {
      std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, rowSubtitle(i).c_str(), rowW - 24);
      renderer.drawText(SMALL_FONT_ID, textX, rowY + 34, subtitle.c_str(), !active);
    }
  }
  drawScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

void RoundedRaffTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2,
                                       const char* btn3, const char* btn4) const {
  LyraTheme::drawButtonHints(renderer, btn1, btn2, btn3, btn4);
}

void RoundedRaffTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, bool isSelected,
                                       const char* secondaryLabel, KeyboardKeyType keyType,
                                       bool inactiveSelection) const {
  (void)secondaryLabel;
  const bool disabled = keyType == KeyboardKeyType::Disabled;
  const bool invert = isSelected && !inactiveSelection && !disabled;
  if (isSelected) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 10, invert ? Color::Black : Color::LightGray);
  }
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 10, true);
  const int fontId = (keyType == KeyboardKeyType::Space || keyType == KeyboardKeyType::Ok) ? UI_10_FONT_ID : UI_12_FONT_ID;
  const auto style = EpdFontFamily::BOLD;
  const int textW = renderer.getTextWidth(fontId, label, style);
  const int textY = rect.y + (rect.height - renderer.getLineHeight(fontId)) / 2;
  renderer.drawText(fontId, rect.x + (rect.width - textW) / 2, textY, label, !invert, style);
}
