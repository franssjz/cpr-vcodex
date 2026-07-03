#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "../fontIds.h"

#include <functional>
#include <string>
#include <vector>

/**
 * Reusable popup overlay for displaying scrollable selection lists.
 * Used by LibraryActivity for Sort/Filter popups.
 *
 * Usage:
 *   LibraryPopupOverlay popup;
 *   popup.title = "Sort by";
 *   popup.items = {"Title A→Z", "Title Z→A", ...};
 *   popup.selectedIndex = 0;
 *   popup.render(renderer, pageWidth, pageHeight);
 */

struct PopupItem {
  std::string label;
  bool selected = false;  // true if currently active (checkmark)
};

class LibraryPopupOverlay {
 public:
  std::string title;
  std::vector<PopupItem> items;
  int selectedIndex = 0;
  int startIndex = 0;  // scroll offset

  static constexpr int kRowH = 38;
  static constexpr int kTitleH = 32;
  static constexpr int kPadX = 12;
  static constexpr int kPadY = 8;
  static constexpr int kPanelW = 360;
  static constexpr int kMaxVisibleRows = 10;
  static constexpr int kCornerRadius = 8;

  int maxPanelH() const {
    int itemCount = static_cast<int>(items.size());
    int visible = std::min(itemCount, kMaxVisibleRows);
    return kTitleH + kPadY + visible * kRowH + 2 * kPadY;
  }

  void render(GfxRenderer& renderer, int pageWidth, int pageHeight) const {
    int itemCount = static_cast<int>(items.size());
    int visibleRows = std::min(itemCount, kMaxVisibleRows);
    int panelH = kTitleH + kPadY + visibleRows * kRowH + 2 * kPadY;
    int panelX = (pageWidth - kPanelW) / 2;
    int panelY = (pageHeight - panelH) / 2;

    // Dim background
    renderer.fillRect(0, 0, pageWidth, panelY, false);
    renderer.fillRect(0, panelY, panelX, panelH, false);
    renderer.fillRect(panelX + kPanelW, panelY, pageWidth - panelX - kPanelW, panelH, false);
    renderer.fillRect(0, panelY + panelH, pageWidth, pageHeight - panelY - panelH, false);

    // Panel background
    renderer.fillRoundedRect(panelX, panelY, kPanelW, panelH, kCornerRadius, Color::White);
    renderer.drawRoundedRect(panelX, panelY, kPanelW, panelH, 2, kCornerRadius, true);

    // Title
    int titleY = panelY + kPadY;
    int titleX = panelX + kPadX;
    int titleW = renderer.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

    // Separator line
    int sepY = titleY + kTitleH + 4;
    renderer.drawLine(panelX + kPadX, sepY, panelX + kPanelW - kPadX, sepY, 1, true);

    // Items
    int listY = sepY + kPadY;
    int endIdx = std::min(startIndex + visibleRows, itemCount);
    for (int i = startIndex; i < endIdx; ++i) {
      int rowY = listY + (i - startIndex) * kRowH;

      // Highlight
      if (i == selectedIndex) {
        renderer.fillRoundedRect(panelX + kPadX, rowY, kPanelW - 2 * kPadX, kRowH - 2, 4, Color::Black);
      }

      // Mark for active item
      std::string rowLabel = items[i].label;
      if (items[i].selected) {
        rowLabel = "* " + rowLabel;
      }

      int textColor = (i == selectedIndex) ? false : true;
      renderer.drawText(SMALL_FONT_ID, panelX + kPadX + 6, rowY + (kRowH - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                        rowLabel.c_str(), textColor, (i == selectedIndex) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }

    // Scroll indicators
    if (startIndex > 0) {
      int arrowY = panelY + kTitleH + kPadY + 2;
      renderer.drawLine(panelX + kPanelW / 2 - 6, arrowY + 6, panelX + kPanelW / 2, arrowY, 2, true);
      renderer.drawLine(panelX + kPanelW / 2, arrowY, panelX + kPanelW / 2 + 6, arrowY + 6, 2, true);
    }
    if (endIdx < itemCount) {
      int arrowY = panelY + panelH - kPadY - 8;
      renderer.drawLine(panelX + kPanelW / 2 - 6, arrowY, panelX + kPanelW / 2, arrowY + 6, 2, true);
      renderer.drawLine(panelX + kPanelW / 2, arrowY + 6, panelX + kPanelW / 2 + 6, arrowY, 2, true);
    }
  }
};