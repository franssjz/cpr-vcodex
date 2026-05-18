#include "RoundedRaffTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kCoverRadius = 18;
constexpr int kPillRadius = 18;
constexpr int kMenuRadius = 30;
constexpr int kBottomRadius = 15;
constexpr int kRowRadius = 20;
constexpr int kInteractiveInsetX = 20;
constexpr int kSelectableRowGap = 6;
constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kSubtitleFontId = SMALL_FONT_ID;
constexpr int kGuideFontId = SMALL_FONT_ID;
int roundedCoverWidth = 0;

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
  int batteryGroupLeftX = batteryX;
  if (showBatteryPercentage) {
    batteryGroupLeftX -= renderer.getTextWidth(SMALL_FONT_ID, "100%") + batteryPercentSpacing;
  }
  const int maxTitleWidth = std::max(0, batteryGroupLeftX - 20 - titleX);
  const auto headerTitle = renderer.truncatedText(kTitleFontId, title, maxTitleWidth, EpdFontFamily::BOLD);
  renderer.drawText(kTitleFontId, titleX, titleY, headerTitle.c_str(), true, EpdFontFamily::BOLD);
  drawBatteryRight(renderer, Rect{batteryX, rect.y + 14, metrics.batteryWidth, metrics.batteryHeight},
                   showBatteryPercentage);
}

void RoundedRaffTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                  bool selected) const {
  if (tabs.empty()) {
    return;
  }
  const int slotWidth = rect.width / static_cast<int>(tabs.size());
  const int tabY = rect.y + 4;
  const int tabHeight = rect.height - 12;
  for (size_t i = 0; i < tabs.size(); ++i) {
    const int slotX = rect.x + static_cast<int>(i) * slotWidth;
    const int tabX = slotX + 4;
    const int tabWidth = slotWidth - 8;
    const bool active = tabs[i].selected;
    if (active) {
      renderer.fillRoundedRect(tabX, tabY, tabWidth, tabHeight, kPillRadius, selected ? Color::Black : Color::DarkGray);
    }
    const int textW = renderer.getTextWidth(kTitleFontId, tabs[i].label, EpdFontFamily::BOLD);
    const int textX = slotX + (slotWidth - textW) / 2;
    const int textY = tabY + (tabHeight - renderer.getLineHeight(kTitleFontId)) / 2;
    renderer.drawText(kTitleFontId, textX, textY, tabs[i].label, !active, EpdFontFamily::BOLD);
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
  const int rowGap = kSelectableRowGap;
  const int rowStep = rowHeight + rowGap;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int pageStart = pageStartForSelection(selectedIndex, pageItems);
  const int rowX = rect.x + metrics.contentSidePadding;
  const int rowW = rect.width - metrics.contentSidePadding * 2 - metrics.scrollBarRightOffset;

  for (int i = pageStart; i < itemCount && i < pageStart + pageItems; ++i) {
    const int rowY = rect.y + (i - pageStart) * rowStep;
    const bool active = selectedIndex == i;
    renderer.fillRoundedRect(rowX, rowY, rowW, rowHeight, kRowRadius, active ? Color::Black : Color::White);

    const int textX = rowX + kInteractiveInsetX;
    const int valueMaxW = rowW / 2 - kInteractiveInsetX;
    std::string value = rowValue ? rowValue(i) : "";
    if (!value.empty()) {
      value = renderer.truncatedText(kTitleFontId, value.c_str(), valueMaxW, EpdFontFamily::REGULAR);
      const int valueW = renderer.getTextWidth(kTitleFontId, value.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(kTitleFontId, rowX + rowW - valueW - kInteractiveInsetX,
                        rowY + (rowHeight - renderer.getLineHeight(kTitleFontId)) / 2, value.c_str(), !active,
                        EpdFontFamily::REGULAR);
    }

    const int titleMaxW = value.empty() ? rowW - kInteractiveInsetX * 2 : rowW - valueMaxW - kInteractiveInsetX * 2;
    std::string title = renderer.truncatedText(kTitleFontId, rowTitle(i).c_str(), titleMaxW, EpdFontFamily::BOLD);
    if (hasSubtitle) {
      const int titleY = rowY + 10;
      const int subtitleY = titleY + renderer.getLineHeight(kTitleFontId) + 4;
      std::string subtitle = renderer.truncatedText(kSubtitleFontId, rowSubtitle(i).c_str(), titleMaxW);
      renderer.drawText(kTitleFontId, textX, titleY, title.c_str(), !active, EpdFontFamily::BOLD);
      renderer.drawText(kSubtitleFontId, textX, subtitleY, subtitle.c_str(), !active);
    } else {
      renderer.drawText(kTitleFontId, textX, rowY + (rowHeight - renderer.getLineHeight(kTitleFontId)) / 2,
                        title.c_str(), !active, EpdFontFamily::BOLD);
    }
  }
  drawScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

void RoundedRaffTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2,
                                       const char* btn3, const char* btn4) const {
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  constexpr int sidePadding = 20;
  constexpr int groupGap = 10;
  constexpr int bottomMargin = 10;
  const int hintHeight = RoundedRaffMetrics::values.buttonHintsHeight - 10;
  const int groupWidth = (pageWidth - sidePadding * 2 - groupGap) / 2;
  const int hintY = pageHeight - hintHeight - bottomMargin;
  const int textY = hintY + (hintHeight - renderer.getLineHeight(kGuideFontId)) / 2;
  const int leftGroupX = sidePadding;
  const int rightGroupX = leftGroupX + groupWidth + groupGap;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  renderer.fillRect(leftGroupX, hintY, groupWidth, hintHeight, false);
  renderer.fillRect(rightGroupX, hintY, groupWidth, hintHeight, false);
  renderer.drawRoundedRect(leftGroupX, hintY, groupWidth, hintHeight, 2, kBottomRadius, true);
  renderer.drawRoundedRect(rightGroupX, hintY, groupWidth, hintHeight, 2, kBottomRadius, true);

  constexpr int innerPadding = 16;
  const int x[] = {
      leftGroupX + innerPadding,
      leftGroupX + groupWidth - innerPadding -
          renderer.getTextWidth(kGuideFontId, labels[1] != nullptr ? labels[1] : "", EpdFontFamily::REGULAR),
      rightGroupX + innerPadding,
      rightGroupX + groupWidth - innerPadding -
          renderer.getTextWidth(kGuideFontId, labels[3] != nullptr ? labels[3] : "", EpdFontFamily::REGULAR),
  };
  for (int i = 0; i < 4; ++i) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      renderer.drawText(kGuideFontId, x[i], textY, labels[i], true, EpdFontFamily::REGULAR);
    }
  }

  renderer.setOrientation(origOrientation);
}

void RoundedRaffTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                      const std::function<std::string(int index)>& buttonLabel,
                                      const std::function<UIIcon(int index)>& rowIcon,
                                      const std::function<std::string(int index)>& buttonSubtitle,
                                      const std::function<bool(int index)>& showAccessory) const {
  (void)rowIcon;
  (void)buttonSubtitle;
  (void)showAccessory;
  const int sidePadding = RoundedRaffMetrics::values.contentSidePadding;
  const int rowX = rect.x + sidePadding;
  const int rowHeight = renderer.getLineHeight(kTitleFontId) + 20;
  const int rowStep = rowHeight + kSelectableRowGap;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int pageStart = pageStartForSelection(selectedIndex, pageItems);
  const int menuMaxWidth = std::max(0, rect.width - sidePadding * 2);

  for (int i = pageStart; i < buttonCount && i < pageStart + pageItems; ++i) {
    constexpr int rowPaddingX = 40;
    const std::string label = renderer.truncatedText(kTitleFontId, buttonLabel(i).c_str(),
                                                     std::max(0, menuMaxWidth - rowPaddingX), EpdFontFamily::BOLD);
    const int rowWidth =
        std::min(menuMaxWidth, renderer.getTextWidth(kTitleFontId, label.c_str(), EpdFontFamily::BOLD) + rowPaddingX);
    const int rowY = rect.y + (i - pageStart) * rowStep;
    const bool selected = selectedIndex == i;
    renderer.fillRoundedRect(rowX, rowY, rowWidth, rowHeight, kMenuRadius, selected ? Color::Black : Color::White);
    renderer.drawText(kTitleFontId, rowX + kInteractiveInsetX,
                      rowY + (rowHeight - renderer.getLineHeight(kTitleFontId)) / 2, label.c_str(), !selected,
                      EpdFontFamily::BOLD);
  }

  drawScrollBar(renderer, rect, buttonCount, pageStart, pageItems);
}

void RoundedRaffTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)selectorIndex;
  (void)bufferRestored;
  const int tileWidth = rect.width - 2 * RoundedRaffMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileY = rect.y;
  const int tileX = RoundedRaffMetrics::values.contentSidePadding;
  const bool hasContinueReading = !recentBooks.empty();
  if (roundedCoverWidth == 0) {
    roundedCoverWidth = RoundedRaffMetrics::values.homeCoverHeight * 0.6;
  }
  const int imgY = tileY + std::max(0, (tileHeight - RoundedRaffMetrics::values.homeCoverHeight) / 2);

  if (hasContinueReading) {
    const RecentBook& book = recentBooks[0];
    if (!coverRendered) {
      bool hasCover = !book.coverBmpPath.empty();
      if (hasCover) {
        const std::string coverBmpPath =
            UITheme::resolveCoverThumbPath(book.coverBmpPath, 0, RoundedRaffMetrics::values.homeCoverHeight);
        FsFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            roundedCoverWidth = bitmap.getWidth();
            renderer.drawBitmap(bitmap, tileX + (tileWidth - roundedCoverWidth) / 2, imgY, roundedCoverWidth,
                                RoundedRaffMetrics::values.homeCoverHeight);
            renderer.maskRoundedRectOutsideCorners(tileX + (tileWidth - roundedCoverWidth) / 2, imgY,
                                                   roundedCoverWidth, RoundedRaffMetrics::values.homeCoverHeight,
                                                   kCoverRadius, Color::LightGray);
          } else {
            hasCover = false;
          }
          file.close();
        } else {
          hasCover = false;
        }
      }

      renderer.drawRoundedRect(tileX + (tileWidth - roundedCoverWidth) / 2, imgY, roundedCoverWidth,
                               RoundedRaffMetrics::values.homeCoverHeight, 1, kCoverRadius, true);
      if (!hasCover) {
        renderer.fillRect(tileX + (tileWidth - roundedCoverWidth) / 2,
                          imgY + RoundedRaffMetrics::values.homeCoverHeight / 3, roundedCoverWidth,
                          2 * RoundedRaffMetrics::values.homeCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + (tileWidth - roundedCoverWidth) / 2 + 24, imgY + 24, 32, 32);
        renderer.maskRoundedRectOutsideCorners(tileX + (tileWidth - roundedCoverWidth) / 2, imgY, roundedCoverWidth,
                                               RoundedRaffMetrics::values.homeCoverHeight, kCoverRadius,
                                               Color::LightGray);
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    }

    renderer.fillRoundedRect(tileX, tileY, tileWidth, imgY - tileY, kRowRadius, true, true, false, false,
                             Color::LightGray);
    renderer.fillRectDither(tileX, imgY, (tileWidth - roundedCoverWidth) / 2,
                            RoundedRaffMetrics::values.homeCoverHeight, Color::LightGray);
    renderer.fillRectDither(tileX + (tileWidth + roundedCoverWidth) / 2, imgY,
                            (tileWidth - roundedCoverWidth) / 2, RoundedRaffMetrics::values.homeCoverHeight,
                            Color::LightGray);
    renderer.fillRoundedRect(tileX, imgY + RoundedRaffMetrics::values.homeCoverHeight, tileWidth,
                             tileHeight - (imgY - tileY + RoundedRaffMetrics::values.homeCoverHeight), kRowRadius,
                             false, false, true, true, Color::LightGray);
  } else {
    renderer.fillRoundedRect(tileX, tileY, tileWidth, tileHeight, kRowRadius, Color::LightGray);
    renderer.drawCenteredText(kTitleFontId, rect.y + rect.height / 2 - renderer.getLineHeight(kTitleFontId) / 2,
                              tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
  }
}

void RoundedRaffTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                                     int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineY = rect.y + rect.height + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  const int thickness = cursorMode ? 3 : 2;
  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY, rect.x + contentStartX + contentWidth - 1, lineY, thickness, true);
    return;
  }
  constexpr int hPadding = 8;
  const int lineW = textWidth + hPadding * 2;
  const int lineStart = rect.x + (rect.width - lineW) / 2;
  renderer.drawLine(lineStart, lineY, lineStart + lineW - 1, lineY, thickness, true);
}

void RoundedRaffTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, bool isSelected,
                                       const char* secondaryLabel, KeyboardKeyType keyType,
                                       bool inactiveSelection) const {
  const bool disabled = keyType == KeyboardKeyType::Disabled;
  const bool invert = isSelected && !inactiveSelection && !disabled;
  if (isSelected) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 10, invert ? Color::Black : Color::LightGray);
  } else {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 10, disabled ? Color::LightGray : Color::White);
  }
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 10, true);

  if (keyType == KeyboardKeyType::Space) {
    const int lineHalfWidth = rect.width * 3 / 10;
    const int centerX = rect.x + rect.width / 2;
    const int lineY = rect.y + rect.height / 2 + 3;
    renderer.drawLine(centerX - lineHalfWidth, lineY, centerX + lineHalfWidth, lineY, 3, !invert);
    return;
  }

  if (keyType == KeyboardKeyType::Del) {
    const int centerX = rect.x + rect.width / 2;
    const int centerY = rect.y + rect.height / 2;
    const int arrowLen = rect.width / 4;
    const int arrowHead = std::max(1, arrowLen / 2);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX + arrowLen / 2, centerY, 3, !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY - arrowHead, 3,
                      !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY + arrowHead, 3,
                      !invert);
    return;
  }

  const int fontId = (keyType == KeyboardKeyType::Space || keyType == KeyboardKeyType::Ok) ? UI_10_FONT_ID : UI_12_FONT_ID;
  const auto style = EpdFontFamily::BOLD;
  if (label != nullptr && label[0] != '\0') {
    const int textW = renderer.getTextWidth(fontId, label, style);
    const int textY = rect.y + (rect.height - renderer.getLineHeight(fontId)) / 2;
    renderer.drawText(fontId, rect.x + (rect.width - textW) / 2, textY, label, !invert, style);
  }

  if (secondaryLabel != nullptr && secondaryLabel[0] != '\0') {
    const int secWidth = renderer.getTextWidth(SMALL_FONT_ID, secondaryLabel);
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - secWidth - 3, rect.y + 1, secondaryLabel, !invert);
  }
}
