#include "FavoritesOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long DELETE_FAVORITE_HOLD_MS = 1000;

std::string getFavoriteTitle(const FavoriteBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }

  const auto slashPos = book.path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? book.path : book.path.substr(slashPos + 1);
  const auto dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}
}  // namespace

void FavoritesOrderActivity::onEnter() {
  Activity::onEnter();
  reloadEntries();
  requestUpdate();
}

void FavoritesOrderActivity::reloadEntries() {
  entries = FAVORITES.getBooks();
  moveMode = !entries.empty() && moveMode;
  if (entries.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(entries.size()) - 1);
  }
}

void FavoritesOrderActivity::moveSelectedEntry(const int delta) {
  const int targetIndex = selectedIndex + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries.size()) || targetIndex == selectedIndex) {
    return;
  }

  if (!FAVORITES.moveBook(selectedIndex, targetIndex)) {
    return;
  }

  std::swap(entries[selectedIndex], entries[targetIndex]);
  selectedIndex = targetIndex;
  requestUpdate();
}

void FavoritesOrderActivity::confirmDeleteSelectedEntry() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  const FavoriteBook selectedEntry = entries[selectedIndex];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_FAVORITES),
                                             getFavoriteTitle(selectedEntry)),
      [this, entryPath = selectedEntry.path](const ActivityResult& result) {
        if (!result.isCancelled) {
          FAVORITES.removeBook(entryPath);
          reloadEntries();
        }
        requestUpdate(true);
      });
}

void FavoritesOrderActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (moveMode) {
      moveMode = false;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!entries.empty()) {
      if (!moveMode && mappedInput.getHeldTime() >= DELETE_FAVORITE_HOLD_MS) {
        confirmDeleteSelectedEntry();
        return;
      }
      moveMode = !moveMode;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) {
      return;
    }
    if (moveMode) {
      moveSelectedEntry(1);
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) {
      return;
    }
    if (moveMode) {
      moveSelectedEntry(-1);
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void FavoritesOrderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ORDER_FAVORITES), tr(STR_FAVORITES_SORT_DESC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_FAVORITES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, listHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) { return getFavoriteTitle(entries[index]); },
                 [this](const int index) {
                   if (!entries[index].author.empty()) {
                     return entries[index].author;
                   }
                   return entries[index].path;
                 },
                 [](const int) { return UIIcon::Heart; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), moveMode ? tr(STR_DONE) : tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
