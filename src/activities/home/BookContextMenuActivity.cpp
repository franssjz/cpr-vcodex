#include "BookContextMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

BookContextMenuActivity::BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const std::string& bookTitle, const bool isFavorite,
                                                 const bool isCompleted, const bool isEpubFormat)
    : Activity("BookContextMenu", renderer, mappedInput),
      menuItems(buildMenuItems(isFavorite, isCompleted, isEpubFormat)),
      bookTitle(bookTitle) {}

std::vector<BookContextMenuActivity::MenuItem> BookContextMenuActivity::buildMenuItems(const bool isFavorite,
                                                                                       const bool isCompleted,
                                                                                       const bool isEpubFormat) {
  std::vector<MenuItem> items;
  items.reserve(7);
  items.push_back({MenuAction::OPEN_BOOK, StrId::STR_OPEN});
  items.push_back({MenuAction::REMOVE_FROM_RECENTS, StrId::STR_DELETE_FROM_RECENTS});
  items.push_back({MenuAction::VIEW_STATS, StrId::STR_READING_STATS});
  items.push_back({MenuAction::VIEW_METADATA, StrId::STR_VIEW_METADATA});
  items.push_back(
      {MenuAction::ADD_TO_FAVORITES, isFavorite ? StrId::STR_REMOVE_FROM_FAVORITES : StrId::STR_ADD_TO_FAVORITES});
  items.push_back(
      {MenuAction::MARK_READ_UNREAD, isCompleted ? StrId::STR_MARK_AS_NOT_FINISHED : StrId::STR_MARK_AS_FINISHED});
  if (isEpubFormat) {
    items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
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
  const int sidePadding = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, bookTitle.c_str());

  int currentY = contentTop;
  currentY += 8;
  renderer.drawLine(sidePadding, currentY, pageWidth - sidePadding, currentY);
  currentY += 8;

  // Menu Items
  const int listTop = currentY;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - 10;
  const auto totalRows = static_cast<int>(menuItems.size());
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, totalRows, selectedIndex,
      [this](int index) {
        if (index >= 0 && index < static_cast<int>(menuItems.size())) {
          return std::string(I18N.get(menuItems[index].labelId));
        }
        return std::string();
      },
      nullptr, nullptr, nullptr, false, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
