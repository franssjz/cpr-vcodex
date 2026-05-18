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
#include "util/RecentBooksGridUi.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}

std::string recentTitle(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const std::string name = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string recentProgress(const RecentBook& book) {
  const auto* statsBook = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
  return statsBook == nullptr ? "" : std::to_string(statsBook->lastProgressPercent) + "%";
}

}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  recentBookCompletedStates.clear();
  recentCoverPaths.clear();
  recentCoverResolvedStates.clear();
  recentProgressLabels.clear();
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
    recentBooks.push_back(resolvedBook);
    recentBookCompletedStates.push_back((statsBook != nullptr && statsBook->completed) ? 1 : 0);
  }
  recentCoverPaths.assign(recentBooks.size(), "");
  recentCoverResolvedStates.assign(recentBooks.size(), 0);
  recentProgressLabels.assign(recentBooks.size(), "");
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
  recentCoverPaths.clear();
  recentCoverResolvedStates.clear();
  recentProgressLabels.clear();
}

void RecentBooksActivity::loadVisiblePageMetadata(const int pageItems) {
  if (recentBooks.empty() || pageItems <= 0) return;
  const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  if (pageStart == loadedPageStart) return;
  const int pageEnd = std::min(pageStart + pageItems, static_cast<int>(recentBooks.size()));
  for (int index = pageStart; index < pageEnd; ++index) {
    if (recentProgressLabels[index].empty()) {
      recentProgressLabels[index] = recentProgress(recentBooks[index]);
    }
    if (index >= static_cast<int>(recentCoverResolvedStates.size()) || recentCoverResolvedStates[index] != 0) {
      continue;
    }
    const std::string coverPath = UITheme::resolveBookCoverThumbPath(
        recentBooks[index].path, recentBooks[index].coverBmpPath, RecentBooksGridUi::kCoverWidth,
        RecentBooksGridUi::kCoverHeight);
    recentCoverPaths[index] = (!coverPath.empty() && Storage.exists(coverPath.c_str())) ? coverPath : "";
    recentCoverResolvedStates[index] = 1;
  }
  loadedPageStart = pageStart;
}

void RecentBooksActivity::loop() {
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems =
      gridView ? RecentBooksGridUi::kMainItemsPerPage
               : UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size())) {
      if (mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
        const RecentBook selectedBook = recentBooks[selectorIndex];
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

      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].path.c_str());
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }

  int listSize = static_cast<int>(recentBooks.size());

  auto moveSelection = [this](const int next) {
    selectorIndex = static_cast<size_t>(next);
    loadVisiblePageMetadata(RecentBooksGridUi::kMainItemsPerPage);
    requestUpdate();
  };

  if (gridView) {
    buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGridUi::moveHorizontal(static_cast<int>(selectorIndex), listSize, true));
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGridUi::moveHorizontal(static_cast<int>(selectorIndex), listSize, false));
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGridUi::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, true));
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGridUi::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, false));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGridUi::moveHorizontal(static_cast<int>(selectorIndex), listSize, true));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, listSize, moveSelection] {
      moveSelection(RecentBooksGridUi::moveHorizontal(static_cast<int>(selectorIndex), listSize, false));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGridUi::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, true));
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, listSize, pageItems, moveSelection] {
      moveSelection(RecentBooksGridUi::moveVertical(static_cast<int>(selectorIndex), listSize, pageItems, false));
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
  const int pageItems = gridView ? RecentBooksGridUi::kMainItemsPerPage
                                 : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
  loadVisiblePageMetadata(pageItems);

  // Recent tab
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else if (!gridView) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, recentBooks.size(), selectorIndex,
        [this](int index) { return recentTitle(recentBooks[index]); },
        [this](int index) {
          return recentBooks[index].author.empty() ? recentProgressLabels[index] : recentBooks[index].author;
        },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); }, nullptr, false,
        [this](int index) { return index >= 0 && index < static_cast<int>(recentBookCompletedStates.size()) &&
                                   recentBookCompletedStates[index] != 0; });
  } else {
    const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
    const int pageEnd = std::min(static_cast<int>(recentBooks.size()), pageStart + pageItems);
    const int gridWidth = RecentBooksGridUi::kColumns * RecentBooksGridUi::kCoverWidth +
                          (RecentBooksGridUi::kColumns - 1) * RecentBooksGridUi::kCoverGap;
    const int startX = (pageWidth - gridWidth) / 2;
    const int metadataTop = contentTop;
    const int gridTop = metadataTop + RecentBooksGridUi::kMetadataBandHeight + RecentBooksGridUi::kCoverGap;

    const std::string title = renderer.truncatedText(UI_10_FONT_ID, recentTitle(recentBooks[selectorIndex]).c_str(),
                                                     pageWidth - metrics.contentSidePadding * 2, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, metadataTop + 2, title.c_str(), true, EpdFontFamily::BOLD);

    std::string detailLine = recentBooks[selectorIndex].author;
    if (!recentProgressLabels[selectorIndex].empty()) {
      if (!detailLine.empty()) detailLine += "  |  ";
      detailLine += recentProgressLabels[selectorIndex];
    }
    if (!detailLine.empty()) {
      const std::string safeDetail =
          renderer.truncatedText(SMALL_FONT_ID, detailLine.c_str(), pageWidth - metrics.contentSidePadding * 2);
      renderer.drawCenteredText(SMALL_FONT_ID, metadataTop + 22, safeDetail.c_str(), true);
    }
    renderer.drawLine(metrics.contentSidePadding, gridTop - 8, pageWidth - metrics.contentSidePadding, gridTop - 8,
                      true);

    for (int index = pageStart; index < pageEnd; ++index) {
      const int local = index - pageStart;
      const int col = local % RecentBooksGridUi::kColumns;
      const int row = local / RecentBooksGridUi::kColumns;
      const Rect cover{startX + col * (RecentBooksGridUi::kCoverWidth + RecentBooksGridUi::kCoverGap),
                       gridTop + row * (RecentBooksGridUi::kCoverHeight + RecentBooksGridUi::kRowGap),
                       RecentBooksGridUi::kCoverWidth, RecentBooksGridUi::kCoverHeight};
      bool drawn = false;
      if (!recentCoverPaths[index].empty()) {
        FsFile file;
        if (Storage.openFileForRead("RBA", recentCoverPaths[index], file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
            float cropX = 0.0f;
            float cropY = 0.0f;
            RecentBooksGridUi::calculateCoverFillCrop(bitmap, cropX, cropY);
            renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height, cropX, cropY);
            drawn = true;
          }
          file.close();
        }
      }
      if (!drawn) RecentBooksGridUi::drawPlaceholder(renderer, cover);
      if (index == static_cast<int>(selectorIndex)) {
        renderer.drawRoundedRect(cover.x - 4, cover.y - 4, cover.width + 8, cover.height + 8, 3,
                                 RecentBooksGridUi::kCoverCornerRadius + 4, true);
        renderer.drawRoundedRect(cover.x - 6, cover.y - 6, cover.width + 12, cover.height + 12, 1,
                                 RecentBooksGridUi::kCoverCornerRadius + 6, true);
      }
    }

    const int totalPages = (static_cast<int>(recentBooks.size()) + pageItems - 1) / pageItems;
    if (totalPages > 1) {
      const int currentPage = selectorIndex / pageItems;
      const int dotSize = 5;
      const int dotGap = 8;
      const int dotsWidth = totalPages * dotSize + (totalPages - 1) * dotGap;
      const int dotsY = contentTop + contentHeight - 12;
      int dotX = (pageWidth - dotsWidth) / 2;
      for (int page = 0; page < totalPages; ++page) {
        if (page == currentPage) {
          renderer.fillRect(dotX, dotsY, dotSize, dotSize, true);
        } else {
          renderer.drawRect(dotX, dotsY, dotSize, dotSize, true);
        }
        dotX += dotSize + dotGap;
      }
    }
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
