#include "BookContextMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/book24.h"
#include "components/icons/heart24.h"
#include "components/icons/trophy24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/image24.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int kPanelPad = 8;
constexpr int kRowH = 44;
constexpr int kIconSize = 24;
constexpr int kIconPad = 10;
constexpr int kCornerRadius = 12;

const uint8_t* iconPixels(UIIcon icon, int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Book:   return Book24Icon;
      case UIIcon::Heart:  return Heart24Icon;
      case UIIcon::Trophy:  return Trophy24Icon;
      case UIIcon::Image:  return Image24Icon;
      default: return nullptr;
    }
  }
  // 32px fallback (Library, Recent, Settings, Transfer icons only exist at 32px)
  switch (icon) {
    case UIIcon::Library:  return LibraryIcon;
    case UIIcon::Recent:   return RecentIcon;
    case UIIcon::Settings: return Settings2Icon;
    case UIIcon::Transfer: return TransferIcon;
    case UIIcon::Book:     return nullptr;  // no 32px variant needed
    default: return nullptr;
  }
}
}  // namespace

BookContextMenuActivity::BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const std::string& bookTitle, const bool isFavorite,
                                                 const bool isCompleted, const bool isEpubFormat, const bool isLibraryMode)
    : Activity("BookContextMenu", renderer, mappedInput),
      menuItems(buildMenuItems(isFavorite, isCompleted, isEpubFormat, isLibraryMode)),
      bookTitle(bookTitle) {}

std::vector<BookContextMenuActivity::MenuItem> BookContextMenuActivity::buildMenuItems(const bool isFavorite,
                                                                                       const bool isCompleted,
                                                                                       const bool isEpubFormat,
                                                                                       const bool isLibraryMode) {
  std::vector<MenuItem> items;
  items.reserve(isLibraryMode ? 15 : 8);
  items.push_back({MenuAction::OPEN_BOOK, StrId::STR_OPEN, UIIcon::Book});
  if (!isLibraryMode) {
    items.push_back({MenuAction::REMOVE_FROM_RECENTS, StrId::STR_DELETE_FROM_RECENTS, UIIcon::Recent});
  }
  items.push_back({MenuAction::VIEW_STATS, StrId::STR_READING_STATS, UIIcon::Trophy});
  items.push_back({MenuAction::VIEW_METADATA, StrId::STR_VIEW_METADATA, UIIcon::Settings});
  items.push_back(
      {MenuAction::ADD_TO_FAVORITES,
       isFavorite ? StrId::STR_REMOVE_FROM_FAVORITES : StrId::STR_ADD_TO_FAVORITES, UIIcon::Heart});
  items.push_back(
      {MenuAction::MARK_READ_UNREAD,
       isCompleted ? StrId::STR_MARK_AS_NOT_FINISHED : StrId::STR_MARK_AS_FINISHED, UIIcon::Heart});
  if (isEpubFormat) {
    items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE, UIIcon::Transfer});
  }
  if (!isLibraryMode) {
    items.push_back({MenuAction::CLEAR_THEME_CACHE, StrId::STR_CLEAR_THEME_CACHE, UIIcon::Transfer});
  }
  if (isLibraryMode) {
    items.push_back({MenuAction::DELETE_COVER_THUMB, StrId::STR_LIBRARY_DELETE_COVER, UIIcon::Image});
    items.push_back({MenuAction::DELETE_PAGE_COVER_THUMBS, StrId::STR_LIBRARY_DELETE_PAGE_COVERS, UIIcon::Image});
    items.push_back({MenuAction::DELETE_ALL_LIBRARY_COVERS, StrId::STR_LIBRARY_DELETE_ALL_COVERS, UIIcon::Image});
    items.push_back({MenuAction::REINDEX_LIBRARY, StrId::STR_REINDEX_LIBRARY, UIIcon::Library});
  }
  return items;
}

void BookContextMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void BookContextMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto selectedAction = menuItems[selectedIndex].action;
    setResult(MenuResult{static_cast<int>(selectedAction), 0, 0});
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
}

void BookContextMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  HeaderDateUtils::drawHeaderWithDate(renderer, bookTitle.c_str());

  // 80% panel centered on screen
  const int panelW = pageWidth * 80 / 100;
  const int panelH = pageHeight * 80 / 100;
  const int panelX = (pageWidth - panelW) / 2;
  const int panelY = (pageHeight - panelH) / 2;

  const int totalItems = static_cast<int>(menuItems.size());
  const int listH = totalItems * kRowH;
  const int scrollY = panelY + kPanelPad;
  const int visibleRows = (panelH - 2 * kPanelPad) / kRowH;

  // Draw rounded panel background
  renderer.fillRoundedRect(panelX, panelY, panelW, panelH, kCornerRadius, Color::White);
  renderer.drawRoundedRect(panelX, panelY, panelW, panelH, 2, kCornerRadius, true);

  int startIdx = 0;
  if (selectedIndex >= visibleRows) {
    startIdx = std::min(selectedIndex - visibleRows / 2, totalItems - visibleRows);
    if (startIdx < 0) startIdx = 0;
  }

  for (int i = 0; i < visibleRows && (startIdx + i) < totalItems; ++i) {
    const int idx = startIdx + i;
    const int y = scrollY + i * kRowH;
    const bool sel = (idx == selectedIndex);

    // Highlight
    if (sel) {
      renderer.fillRect(panelX + kPanelPad, y, panelW - 2 * kPanelPad, kRowH, true);
    }

    // Icon
    const uint8_t* iconBmp = iconPixels(menuItems[idx].icon, kIconSize);
    if (iconBmp) {
      if (sel) {
        renderer.drawIconInverted(iconBmp, panelX + kPanelPad + kIconPad, y + (kRowH - kIconSize) / 2, kIconSize, kIconSize);
      } else {
        renderer.drawIcon(iconBmp, panelX + kPanelPad + kIconPad, y + (kRowH - kIconSize) / 2, kIconSize, kIconSize);
      }
    }

    // Label
    const char* label = I18N.get(menuItems[idx].labelId);
    int lx = panelX + kPanelPad + kIconPad + kIconSize + kIconPad;
    int ly = y + (kRowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, lx, ly, label, sel ? false : true, EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}