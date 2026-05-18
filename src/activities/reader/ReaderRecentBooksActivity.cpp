#include "ReaderRecentBooksActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RecentBooksGrid.h"

namespace {
constexpr int MAX_QUICK_RECENT_BOOKS = 6;

}  // namespace

ReaderRecentBooksActivity::ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const std::string& currentPath)
    : Activity("ReaderRecentBooks", renderer, mappedInput), currentPath(currentPath) {}

void ReaderRecentBooksActivity::loadBooks() {
  books.clear();
  books.reserve(MAX_QUICK_RECENT_BOOKS);
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.path.empty() || book.path == currentPath || !Storage.exists(book.path.c_str())) {
      continue;
    }
    RecentBook resolvedBook = book;
    const auto* statsBook = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
    if (resolvedBook.coverBmpPath.empty() && statsBook != nullptr && !statsBook->coverBmpPath.empty()) {
      resolvedBook.coverBmpPath = statsBook->coverBmpPath;
    }
    books.push_back(RecentBooksGrid::BookState{resolvedBook});
    if (books.size() >= MAX_QUICK_RECENT_BOOKS) {
      break;
    }
  }
  selectedIndex = 0;
  loadedPageStart = -1;
  loadVisiblePageMetadata();
}

void ReaderRecentBooksActivity::onEnter() {
  Activity::onEnter();
  loadBooks();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

void ReaderRecentBooksActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (books.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    setResult(KeyboardResult{books[selectedIndex].book.path});
    finish();
    return;
  }

  const int total = static_cast<int>(books.size());
  if (total <= 0) {
    return;
  }
  auto moveSelection = [this, total](const int next) {
    selectedIndex = next;
    loadVisiblePageMetadata();
    requestUpdate();
  };
  buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, true));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, false));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, RecentBooksGrid::kQuickItemsPerPage, true));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, RecentBooksGrid::kQuickItemsPerPage, false));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, true));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, false));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, RecentBooksGrid::kQuickItemsPerPage, true));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, RecentBooksGrid::kQuickItemsPerPage, false));
  });
}

void ReaderRecentBooksActivity::loadVisiblePageMetadata() {
  if (books.empty()) return;
  const int pageStart = (selectedIndex / RecentBooksGrid::kQuickItemsPerPage) * RecentBooksGrid::kQuickItemsPerPage;
  RecentBooksGrid::ensurePageProgress(books, pageStart, RecentBooksGrid::kQuickItemsPerPage);
}

void ReaderRecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_RECENT_BOOKS));

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_OPEN_BOOK));
  } else {
    const int currentPage = selectedIndex / RecentBooksGrid::kQuickItemsPerPage;
    const int pageStart = currentPage * RecentBooksGrid::kQuickItemsPerPage;
    const int pageCount = std::min(RecentBooksGrid::kQuickItemsPerPage, static_cast<int>(books.size()) - pageStart);
    const int gridWidth = RecentBooksGrid::kColumns * RecentBooksGrid::kCoverWidth +
                          (RecentBooksGrid::kColumns - 1) * RecentBooksGrid::kGridSpacing;
    const int startX = (pageWidth - gridWidth) / 2;
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int metadataTop = contentTop;

    RecentBooksGrid::drawSelectedTitle(renderer, books, selectedIndex, startX, metadataTop, gridWidth);

    const int gridTop = contentTop + RecentBooksGrid::kTitleStripHeight + RecentBooksGrid::kTitleGridGap;
    renderer.drawLine(metrics.contentSidePadding, gridTop - 8, pageWidth - metrics.contentSidePadding, gridTop - 8,
                      true);
    RecentBooksGrid::drawGrid(renderer, books, selectedIndex, pageStart, pageCount, startX, gridTop);

    const int totalPages =
        (static_cast<int>(books.size()) + RecentBooksGrid::kQuickItemsPerPage - 1) / RecentBooksGrid::kQuickItemsPerPage;
    if (totalPages > 1) {
      const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 10;
      RecentBooksGrid::drawPageDots(renderer, pageWidth, dotY, totalPages, currentPage);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();

  if (!books.empty()) {
    const int pageStart = (selectedIndex / RecentBooksGrid::kQuickItemsPerPage) * RecentBooksGrid::kQuickItemsPerPage;
    if (pageStart != loadedPageStart) {
      const bool generated =
          RecentBooksGrid::loadPageCovers(renderer, books, pageStart, RecentBooksGrid::kQuickItemsPerPage);
      loadedPageStart = pageStart;
      if (generated) {
        requestUpdate();
      }
    }
  }
}
