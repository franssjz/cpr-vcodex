#include "FavoritesAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FavoritesBrowserActivity.h"
#include "FavoritesOrderActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int ACTION_COUNT = 2;
}  // namespace

void FavoritesAppActivity::refreshEntries() {
  favoriteCount = static_cast<int>(FAVORITES.getBooks().size());
  selectedIndex = std::clamp(selectedIndex, 0, ACTION_COUNT - 1);
}

void FavoritesAppActivity::openSelectedEntry() {
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<FavoritesBrowserActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             refreshEntries();
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == 1) {
    startActivityForResult(std::make_unique<FavoritesOrderActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             refreshEntries();
                             requestUpdate();
                           });
    return;
  }
}

void FavoritesAppActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();
  requestUpdate();
}

void FavoritesAppActivity::loop() {
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedEntry();
    return;
  }

  const int itemCount = ACTION_COUNT;
  buttonNavigator.onNextRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, itemCount, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, itemCount, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
  });
}

void FavoritesAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const std::string headerSubtitle = std::to_string(favoriteCount);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FAVORITES), headerSubtitle.c_str());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, listHeight}, ACTION_COUNT, selectedIndex,
      [this](const int index) {
        if (index == 0) return std::string(tr(STR_BROWSE_FILES));
        if (index == 1) return std::string(tr(STR_ORDER_FAVORITES));
        return std::string();
      },
      [this](const int index) {
        if (index == 0) return std::string(tr(STR_FAVORITES_BROWSER_DESC));
        if (index == 1) return std::string(tr(STR_FAVORITES_SORT_DESC));
        return std::string();
      },
      [this](const int index) {
        if (index == 0) return UIIcon::Folder;
        if (index == 1) return UIIcon::Settings;
        return UIIcon::Heart;
      });

  if (favoriteCount == 0) {
    renderer.drawCenteredText(SMALL_FONT_ID, contentTop + listHeight - 14, tr(STR_NO_FAVORITES));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
