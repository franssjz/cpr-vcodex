#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int batteryPercentSpacing = 4;
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
constexpr int popupMarginX = 16;
constexpr int popupMarginY = 12;
constexpr int maxSubtitleWidth = 100;
constexpr int maxListValueWidth = 200;
constexpr int mainMenuIconSize = 32;
constexpr int listIconSize = 24;
constexpr int mainMenuColumns = 2;
int coverWidth = 0;

void drawLyraBatteryIcon(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight,
                         uint16_t percentage) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);

  const bool charging = gpio.isUsbConnected();

  // Draw bars
  if (percentage > 10 || charging) {
    renderer.fillRect(x + 2, y + 2, 3, rectHeight - 4);
  }
  if (percentage > 40 || charging) {
    renderer.fillRect(x + 6, y + 2, 3, rectHeight - 4);
  }
  if (percentage > 70) {
    renderer.fillRect(x + 10, y + 2, 3, rectHeight - 4);
  }

  if (charging) {
    const int boltX = x + 4;
    const int boltY = y + 2;
    renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
    renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
    renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
    renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
    renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
    renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
    renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
    renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
  }
}

const uint8_t* iconForName(UIIcon icon, int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      default:
        return nullptr;
    }
  } else if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::Recent:
        return RecentIcon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Transfer:
        return TransferIcon;
      case UIIcon::Library:
        return LibraryIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      default:
        return nullptr;
    }
  }
  return nullptr;
}

int fallbackIconSize(UIIcon icon, int requestedSize) {
  if (requestedSize == 32) {
    switch (icon) {
      case UIIcon::Text:
      case UIIcon::File:
      case UIIcon::Image:
        return 24;
      default:
        return 0;
    }
  }
  return 0;
}

void drawCompletedListBadge(const GfxRenderer& renderer, const int x, const int y, const int size) {
  const int safeSize = std::max(12, size);
  const int strokeWidth = safeSize >= 16 ? 2 : 1;

  renderer.fillRect(x, y, safeSize, safeSize, true);
  renderer.drawRect(x, y, safeSize, safeSize, false);

  const int leftX = x + safeSize * 3 / 16;
  const int midX = x + safeSize * 7 / 16;
  const int rightX = x + safeSize * 13 / 16;
  const int leftY = y + safeSize * 9 / 16;
  const int midY = y + safeSize * 12 / 16;
  const int rightY = y + safeSize * 4 / 16;

  renderer.drawLine(leftX, leftY, midX, midY, strokeWidth, false);
  renderer.drawLine(midX, midY, rightX, rightY, strokeWidth, false);
}
}  // namespace

void LyraTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getBatteryPercentage();

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + LyraMetrics::values.batteryWidth, rect.y,
                      percentageText.c_str());
  }

  drawLyraBatteryIcon(renderer, rect.x, rect.y + 6, LyraMetrics::values.batteryWidth, rect.height, percentage);
}

void LyraTheme::drawBatteryRight(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Right aligned: percentage on left, icon on right (UI headers)
  const uint16_t percentage = powerManager.getBatteryPercentage();

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
    // Clear the area where we're going to draw the text to prevent ghosting
    const auto textHeight = renderer.getTextHeight(SMALL_FONT_ID);
    renderer.fillRect(rect.x - textWidth - batteryPercentSpacing, rect.y, textWidth, textHeight, false);
    // Draw text to the left of the icon
    renderer.drawText(SMALL_FONT_ID, rect.x - textWidth - batteryPercentSpacing, rect.y, percentageText.c_str());
  }

  drawLyraBatteryIcon(renderer, rect.x, rect.y + 6, LyraMetrics::values.batteryWidth, rect.height, percentage);
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                           const char* titleDetail) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - 12 - LyraMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
                   showBatteryPercentage);

  int maxTitleWidth =
      rect.width - LyraMetrics::values.contentSidePadding * 2 - (subtitle != nullptr ? maxSubtitleWidth : 0);

  if (title) {
    const int titleX = rect.x + LyraMetrics::values.contentSidePadding;
    const int titleY = rect.y + LyraMetrics::values.batteryBarHeight + 3;

    if (titleDetail && titleDetail[0] != '\0') {
      const int detailY =
          titleY + std::max(0, (renderer.getTextHeight(UI_12_FONT_ID) - renderer.getTextHeight(SMALL_FONT_ID)) / 2);
      constexpr int inlineGap = 6;

      auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
      int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, truncatedTitle.c_str(), EpdFontFamily::BOLD);
      int remainingWidth = std::max(0, maxTitleWidth - titleWidth - inlineGap);

      std::string truncatedDetail;
      if (remainingWidth > 12) {
        truncatedDetail =
            renderer.truncatedText(SMALL_FONT_ID, titleDetail, remainingWidth, EpdFontFamily::REGULAR);
      }

      renderer.drawText(UI_12_FONT_ID, titleX, titleY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
      if (!truncatedDetail.empty()) {
        renderer.drawText(SMALL_FONT_ID, titleX + titleWidth + inlineGap, detailY, truncatedDetail.c_str(), true,
                          EpdFontFamily::REGULAR);
      }
    } else {
      auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, titleX, titleY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
    }
    renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width - 1, rect.y + rect.height - 3, 3, true);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - LyraMetrics::values.contentSidePadding - truncatedSubtitleWidth,
                      rect.y + 50, truncatedSubtitle.c_str(), true);
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;
  int rightSpace = LyraMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + hPaddingInSelection;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_10_FONT_ID, label, rect.width - LyraMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }

  const int availableWidth = std::max(0, rect.width - LyraMetrics::values.contentSidePadding * 2);
  int totalWidth = 0;
  for (size_t i = 0; i < tabs.size(); ++i) {
    totalWidth += renderer.getTextWidth(UI_10_FONT_ID, tabs[i].label, EpdFontFamily::REGULAR) + 2 * hPaddingInSelection;
    if (i + 1 < tabs.size()) {
      totalWidth += LyraMetrics::values.tabSpacing;
    }
  }

  const bool useCompactLayout = !tabs.empty() && totalWidth > availableWidth;
  if (!useCompactLayout) {
    int currentX = rect.x + LyraMetrics::values.contentSidePadding;

    for (const auto& tab : tabs) {
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR);

      if (tab.selected) {
        if (selected) {
          renderer.fillRoundedRect(currentX, rect.y + 1, textWidth + 2 * hPaddingInSelection, rect.height - 4,
                                   cornerRadius, Color::Black);
        } else {
          renderer.fillRectDither(currentX, rect.y, textWidth + 2 * hPaddingInSelection, rect.height - 3,
                                  Color::LightGray);
          renderer.drawLine(currentX, rect.y + rect.height - 3, currentX + textWidth + 2 * hPaddingInSelection,
                            rect.y + rect.height - 3, 2, true);
        }
      }

      renderer.drawText(UI_10_FONT_ID, currentX + hPaddingInSelection, rect.y + 6, tab.label,
                        !(tab.selected && selected), EpdFontFamily::REGULAR);

      currentX += textWidth + LyraMetrics::values.tabSpacing + 2 * hPaddingInSelection;
    }
  } else {
    const int slotStartX = rect.x + LyraMetrics::values.contentSidePadding;
    for (size_t i = 0; i < tabs.size(); ++i) {
      const auto& tab = tabs[i];
      const int slotX = slotStartX + (availableWidth * static_cast<int>(i)) / static_cast<int>(tabs.size());
      const int nextSlotX = slotStartX + (availableWidth * static_cast<int>(i + 1)) / static_cast<int>(tabs.size());
      const int slotWidth = nextSlotX - slotX;
      const int textMaxWidth = std::max(0, slotWidth - 2 * hPaddingInSelection - 4);
      const std::string label = renderer.truncatedText(UI_10_FONT_ID, tab.label, textMaxWidth, EpdFontFamily::REGULAR);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str(), EpdFontFamily::REGULAR);
      const int textX = slotX + std::max(0, (slotWidth - textWidth) / 2);

      if (tab.selected) {
        if (selected) {
          renderer.fillRoundedRect(slotX, rect.y + 1, slotWidth, rect.height - 4, cornerRadius, Color::Black);
        } else {
          renderer.fillRectDither(slotX, rect.y, slotWidth, rect.height - 3, Color::LightGray);
          renderer.drawLine(slotX, rect.y + rect.height - 3, slotX + slotWidth, rect.y + rect.height - 3, 2, true);
        }
      }

      renderer.drawText(UI_10_FONT_ID, textX, rect.y + 6, label.c_str(), !(tab.selected && selected),
                        EpdFontFamily::REGULAR);
    }
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowCompleted) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  int pageItems = rect.height / rowHeight;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(LyraMetrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
                             contentWidth - LyraMetrics::values.contentSidePadding * 2, rowHeight, cornerRadius,
                             Color::LightGray);
  }

  int textX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
  int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2;
  int iconSize;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSize : listIconSize;
    textX += iconSize + hPaddingInSelection;
    textWidth -= iconSize + hPaddingInSelection;
  }

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int rowTextWidth = textWidth;

    // Draw name
    int valueWidth = 0;
    std::string valueText = "";
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxListValueWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPaddingInSelection;
      rowTextWidth -= valueWidth;
    }

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 7, item.c_str(), true);

    if (rowIcon != nullptr) {
      const int iconBaseX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
      const int iconBaseY = itemY + iconY;

      if (rowCompleted != nullptr && rowCompleted(i)) {
        const int badgeSize = iconSize >= 32 ? 18 : 14;
        const int badgeX = iconBaseX + std::max(0, (iconSize - badgeSize) / 2);
        const int badgeY = iconBaseY + std::max(0, (iconSize - badgeSize) / 2);
        drawCompletedListBadge(renderer, badgeX, badgeY, badgeSize);
      } else {
        UIIcon icon = rowIcon(i);
        int drawSize = iconSize;
        const uint8_t* iconBitmap = iconForName(icon, drawSize);
        if (iconBitmap == nullptr) {
          const int fallbackSize = fallbackIconSize(icon, iconSize);
          if (fallbackSize > 0) {
            drawSize = fallbackSize;
            iconBitmap = iconForName(icon, drawSize);
          }
        }
        if (iconBitmap != nullptr) {
          const int iconX = iconBaseX + std::max(0, (iconSize - drawSize) / 2);
          const int adjustedIconY = iconBaseY + std::max(0, (iconSize - drawSize) / 2);
          renderer.drawIcon(iconBitmap, iconX, adjustedIconY, drawSize, drawSize);
        }
      }
    }

    if (rowSubtitle != nullptr) {
      // Draw subtitle
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
      renderer.drawText(SMALL_FONT_ID, textX, itemY + 30, subtitle.c_str(), true);
    }

    // Draw value
    if (!valueText.empty()) {
      if (i == selectedIndex && highlightValue) {
        renderer.fillRoundedRect(
            contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueWidth, itemY,
            valueWidth + hPaddingInSelection, rowHeight, cornerRadius, Color::Black);
      }

      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - LyraMetrics::values.contentSidePadding - valueWidth,
                        itemY + 6, valueText.c_str(), !(i == selectedIndex && highlightValue));
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  constexpr int x4ButtonPositions[] = {58, 146, 254, 342};
  constexpr int x3ButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               Color::White);
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 0;

  if (gpio.deviceIsX3()) {
    constexpr int x3ButtonY = 155;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(buttonMargin, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, false, true, false,
                               true, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, buttonMargin, x3ButtonY + (buttonHeight + textWidth) / 2, topBtn);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int rightX = screenWidth - buttonWidth;
      renderer.drawRoundedRect(rightX, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, rightX, x3ButtonY + (buttonHeight + textWidth) / 2, bottomBtn);
    }
  } else {
    const char* labels[] = {topBtn, bottomBtn};
    const int x = screenWidth - buttonWidth;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                               false, true, false, true);
    }

    for (int i = 0; i < 2; i++) {
      if (labels[i] != nullptr && labels[i][0] != '\0') {
        const int y = topHintButtonY + (i * buttonHeight + 5);
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, labels[i]);
      }
    }
  }
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = rect.width - 2 * LyraMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();
  if (coverWidth == 0) {
    coverWidth = LyraMetrics::values.homeCoverHeight * 0.6;
  }

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    RecentBook book = recentBooks[0];
    if (!coverRendered) {
      std::string coverPath = book.coverBmpPath;
      bool hasCover = true;
      int tileX = LyraMetrics::values.contentSidePadding;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, LyraMetrics::values.homeCoverHeight);

        // First time: load cover from SD and render
        FsFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            coverWidth = bitmap.getWidth();
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                LyraMetrics::values.homeCoverHeight);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      // Draw either way
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                        LyraMetrics::values.homeCoverHeight, true);

      if (!hasCover) {
        // Render empty cover
        renderer.fillRect(tileX + hPaddingInSelection,
                          tileY + hPaddingInSelection + (LyraMetrics::values.homeCoverHeight / 3), coverWidth,
                          2 * LyraMetrics::values.homeCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + 24, 32, 32);
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    bool bookSelected = (selectorIndex == 0);

    int tileX = LyraMetrics::values.contentSidePadding;
    int textWidth = tileWidth - 2 * hPaddingInSelection - LyraMetrics::values.verticalSpacing - coverWidth;

    if (bookSelected) {
      // Draw selection box
      renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                              LyraMetrics::values.homeCoverHeight, Color::LightGray);
      renderer.fillRectDither(tileX + hPaddingInSelection + coverWidth, tileY + hPaddingInSelection,
                              tileWidth - hPaddingInSelection - coverWidth, LyraMetrics::values.homeCoverHeight,
                              Color::LightGray);
      renderer.fillRoundedRect(tileX, tileY + LyraMetrics::values.homeCoverHeight + hPaddingInSelection, tileWidth,
                               hPaddingInSelection, cornerRadius, false, false, true, true, Color::LightGray);
    }

    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 3, EpdFontFamily::BOLD);

    auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
    const int authorHeight = book.author.empty() ? 0 : (renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2);
    const int totalBlockHeight = titleBlockHeight + authorHeight;
    int titleY = tileY + tileHeight / 2 - totalBlockHeight / 2;
    const int textX = tileX + hPaddingInSelection + coverWidth + LyraMetrics::values.verticalSpacing;
    for (const auto& line : titleLines) {
      renderer.drawText(UI_12_FONT_ID, textX, titleY, line.c_str(), true, EpdFontFamily::BOLD);
      titleY += titleLineHeight;
    }
    if (!book.author.empty()) {
      titleY += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, titleY, author.c_str(), true);
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void LyraTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  constexpr int padding = 48;
  renderer.drawText(UI_12_FONT_ID, rect.x + padding,
                    rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) - 2, tr(STR_NO_OPEN_BOOK), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, rect.y + rect.height / 2 + 2, tr(STR_START_READING), true);
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon,
                               const std::function<std::string(int index)>& buttonSubtitle,
                               const std::function<bool(int index)>& showAccessory) const {
  const int availableHeight = std::max(0, rect.height);
  const int gap = LyraMetrics::values.menuSpacing;
  const int rowHeight = buttonCount > 0
                            ? std::min(LyraMetrics::values.menuRowHeight,
                                       (availableHeight - gap * std::max(0, buttonCount - 1)) / buttonCount)
                            : LyraMetrics::values.menuRowHeight;

  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect = Rect{rect.x + LyraMetrics::values.contentSidePadding,
                         rect.y + i * (rowHeight + gap), tileWidth, rowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      int drawSize = mainMenuIconSize;
      const uint8_t* iconBitmap = iconForName(icon, drawSize);
      if (iconBitmap == nullptr) {
        const int fallbackSize = fallbackIconSize(icon, mainMenuIconSize);
        if (fallbackSize > 0) {
          drawSize = fallbackSize;
          iconBitmap = iconForName(icon, drawSize);
        }
      }
      if (iconBitmap != nullptr) {
        const int iconX = textX + std::max(0, (mainMenuIconSize - drawSize) / 2);
        const int iconY = tileRect.y + (tileRect.height - drawSize) / 2 + 1;
        renderer.drawIcon(iconBitmap, iconX, iconY, drawSize, drawSize);
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    const std::string subtitle = buttonSubtitle ? buttonSubtitle(i) : std::string();
    const bool hasSubtitle = !subtitle.empty();
    if (hasSubtitle) {
      renderer.drawText(UI_12_FONT_ID, textX, tileRect.y + 8, label, true);

      const int badgeWidth = showAccessory && showAccessory(i) ? 20 : 0;
      const int subtitleMaxWidth = tileRect.width - (textX - tileRect.x) - 18 - badgeWidth;
      std::string subtitleStr =
          renderer.truncatedText(SMALL_FONT_ID, subtitle.c_str(), subtitleMaxWidth, EpdFontFamily::REGULAR);
      const int subtitleY = tileRect.y + 36;
      renderer.drawText(SMALL_FONT_ID, textX, subtitleY, subtitleStr.c_str(), true);

      if (showAccessory && showAccessory(i)) {
        const int badgeSize = 16;
        const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, subtitleStr.c_str());
        const int badgeX = std::min(tileRect.x + tileRect.width - badgeSize - 10, textX + subtitleWidth + 8);
        const int badgeY = subtitleY - 1;
        renderer.fillRect(badgeX, badgeY, badgeSize, badgeSize, true);
        renderer.drawLine(badgeX + 3, badgeY + 8, badgeX + 6, badgeY + 11, 2, false);
        renderer.drawLine(badgeX + 6, badgeY + 11, badgeX + 12, badgeY + 4, 2, false);
      }
    } else {
      const int textY = tileRect.y + (rowHeight - titleLineHeight) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
    }
  }
}

Rect LyraTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  constexpr int y = 132;
  constexpr int outline = 2;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + popupMarginX * 2;
  const int h = textHeight + popupMarginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRoundedRect(x - outline, y - outline, w + outline * 2, h + outline * 2, cornerRadius + outline,
                           Color::White);
  renderer.fillRoundedRect(x, y, w, h, cornerRadius, Color::Black);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + popupMarginY - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, false, EpdFontFamily::REGULAR);
  renderer.displayBuffer();

  return Rect{x, y, w, h};
}

void LyraTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  constexpr int barHeight = 4;

  // Twice the margin in drawPopup to match text width
  const int barWidth = layout.width - popupMarginX * 2;
  const int barX = layout.x + (layout.width - barWidth) / 2;
  // Center inside the margin of drawPopup. The - 1 is added to account for the - 2 in drawPopup.
  const int barY = layout.y + layout.height - popupMarginY / 2 - barHeight / 2 - 1;

  int fillWidth = barWidth * progress / 100;

  renderer.fillRect(barX, barY, fillWidth, barHeight, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void LyraTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth) const {
  int lineY = rect.y + rect.height + renderer.getLineHeight(UI_12_FONT_ID) + LyraMetrics::values.verticalSpacing;
  int lineW = textWidth + hPaddingInSelection * 2;
  renderer.drawLine(rect.x + (rect.width - lineW) / 2, lineY, rect.x + (rect.width + lineW) / 2, lineY, 3);
}

void LyraTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label,
                                const bool isSelected) const {
  if (isSelected) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cornerRadius, Color::Black);
  }

  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, !isSelected);
}
