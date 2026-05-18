#include "RecentBooksActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RecentBooksGrid.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}

}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  recentBookCompletedStates.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(books.size());
  recentBookCompletedStates.reserve(books.size());

  for (const auto& book : books) {
    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str()) || (!APP_STATE.openEpubPath.empty() && book.path == APP_STATE.openEpubPath)) {
      continue;
    }
    const auto* statsBook = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
    RecentBook resolvedBook = book;
    if (resolvedBook.coverBmpPath.empty() && statsBook != nullptr && !statsBook->coverBmpPath.empty()) {
      resolvedBook.coverBmpPath = statsBook->coverBmpPath;
    }
    recentBooks.push_back(RecentBooksGrid::BookState{resolvedBook});
    recentBookCompletedStates.push_back((statsBook != nullptr && statsBook->completed) ? 1 : 0);
  }
  loadedPageStart = -1;
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadRecentBooks();

  selectorIndex = 0;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  recentBookCompletedStates.clear();
}

void RecentBooksActivity::loadVisiblePageMetadata(const int pageItems) {
  if (recentBooks.empty() || pageItems <= 0) return;
  const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  RecentBooksGrid::ensurePageProgress(recentBooks, pageStart, pageItems);
}

void RecentBooksActivity::loop() {
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems =
      gridView ? RecentBooksGrid::kItemsPerPage
               : UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size())) {
      if (mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
        const RecentBook selectedBook = recentBooks[selectorIndex].book;
        const size_t currentSelection = selectorIndex;
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_RECENTS),
                                                   getRecentBookConfirmationLabel(selectedBook)),
            [this, selectedBook, currentSelection](const ActivityResult& result) {
              if (result.isCancelled) {
                requestUpdate();
                return;
              }

              if (RECENT_BOOKS.removeBook(selectedBook.path)) {
                loadRecentBooks();
                if (recentBooks.empty()) {
                  selectorIndex = 0;
                } else if (currentSelection >= recentBooks.size()) {
                  selectorIndex = recentBooks.size() - 1;
                } else {
                  selectorIndex = currentSelection;
                }
              }
              requestUpdate(true);
            });
        return;
      }

      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].book.path.c_str());
      onSelectBook(recentBooks[selectorIndex].book.path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }

  int listSize = static_cast<int>(recentBooks.size());

  auto moveSelection = [this](const int next) {
    selectorIndex = static_cast<size_t>(next);
    loadVisiblePageMetadata(RecentBooksGrid::kItemsPerPage);
    requestUpdate();
  };

  if (gridView) {
    buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGrid::moveHorizontal(static_cast<int>(selectorIndex), listSize, true));
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGrid::moveHorizontal(static_cast<int>(selectorIndex), listSize, false));
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGrid::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, true));
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGrid::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, false));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGrid::moveHorizontal(static_cast<int>(selectorIndex), listSize, true));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGrid::moveHorizontal(static_cast<int>(selectorIndex), listSize, false));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGrid::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, true));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGrid::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, false));
    });
  } else {
    buttonNavigator.onNextRelease([this, listSize] {
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, listSize] {
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this, listSize, pageItems] {
      selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
      selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
      requestUpdate();
    });
  }
}

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems = gridView ? RecentBooksGrid::kItemsPerPage
                                 : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
  loadVisiblePageMetadata(pageItems);

  // Recent tab
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else if (!gridView) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, recentBooks.size(), selectorIndex,
        [this](int index) { return RecentBooksGrid::titleFor(recentBooks[index].book); },
        [this](int index) {
          return recentBooks[index].book.author.empty() ? recentBooks[index].progressLabel : recentBooks[index].book.author;
        },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].book.path); }, nullptr, false,
        [this](int index) { return index >= 0 && index < static_cast<int>(recentBookCompletedStates.size()) &&
                                   recentBookCompletedStates[index] != 0; });
  } else {
    const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
    const int pageEnd = std::min(static_cast<int>(recentBooks.size()), pageStart + pageItems);
    const int gridWidth = RecentBooksGrid::kColumns * RecentBooksGrid::kCoverWidth +
                          (RecentBooksGrid::kColumns - 1) * RecentBooksGrid::kGridSpacing;
    const int startX = (pageWidth - gridWidth) / 2;
    const int metadataTop = contentTop;
    const int gridTop = metadataTop + RecentBooksGrid::kTitleStripHeight + RecentBooksGrid::kTitleGridGap;

    RecentBooksGrid::drawSelectedTitle(renderer, recentBooks, static_cast<int>(selectorIndex), startX, metadataTop,
                                       gridWidth);
    renderer.drawLine(metrics.contentSidePadding, gridTop - 8, pageWidth - metrics.contentSidePadding, gridTop - 8,
                      true);
    RecentBooksGrid::drawGrid(renderer, recentBooks, static_cast<int>(selectorIndex), pageStart, pageEnd - pageStart,
                              startX, gridTop);

    const int totalPages = (static_cast<int>(recentBooks.size()) + pageItems - 1) / pageItems;
    if (totalPages > 1) {
      const int currentPage = selectorIndex / pageItems;
      const int dotsY = contentTop + contentHeight - 12;
      RecentBooksGrid::drawPageDots(renderer, pageWidth, dotsY, totalPages, currentPage);
    }
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (gridView && !recentBooks.empty()) {
    const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
    if (pageStart != loadedPageStart) {
      const bool generated = RecentBooksGrid::loadPageCovers(renderer, recentBooks, pageStart, pageItems);
      loadedPageStart = pageStart;
      if (generated) {
        requestUpdate();
      }
    }
  }
}
