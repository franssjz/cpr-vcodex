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

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
constexpr int RECENT_GRID_COLUMNS = 3;
constexpr int RECENT_GRID_ITEMS = 9;
constexpr int RECENT_COVER_W = 120;
constexpr int RECENT_COVER_H = 176;
constexpr int RECENT_GRID_GAP = 8;

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

void drawRecentPlaceholder(GfxRenderer& renderer, const Rect& rect) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 4, true);
  const int bookW = std::min(rect.width - 12, std::max(34, rect.width * 2 / 3));
  const int bookH = std::min(rect.height - 12, std::max(48, rect.height - 18));
  const int bookX = rect.x + (rect.width - bookW) / 2;
  const int bookY = rect.y + (rect.height - bookH) / 2;
  renderer.drawRoundedRect(bookX, bookY, bookW, bookH, 2, 4, true);
  renderer.drawLine(bookX + 8, bookY + 4, bookX + 8, bookY + bookH - 5, true);
  renderer.drawLine(bookX + 14, bookY + 12, bookX + bookW - 9, bookY + 12, 2, true);
  renderer.drawLine(bookX + 14, bookY + 24, bookX + bookW - 12, bookY + 24, true);
  renderer.drawLine(bookX + 14, bookY + 36, bookX + bookW - 16, bookY + 36, true);
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  recentBookCompletedStates.clear();
  recentCoverPaths.clear();
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
  recentProgressLabels.clear();
}

void RecentBooksActivity::loadVisiblePageMetadata(const int pageItems) {
  if (recentBooks.empty() || pageItems <= 0) return;
  const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  if (pageStart == loadedPageStart) return;
  const int pageEnd = std::min(pageStart + pageItems, static_cast<int>(recentBooks.size()));
  for (int index = pageStart; index < pageEnd; ++index) {
    recentProgressLabels[index] = recentProgress(recentBooks[index]);
    if (!recentBooks[index].coverBmpPath.empty()) {
      const std::string coverPath =
          UITheme::resolveCoverThumbPath(recentBooks[index].coverBmpPath, RECENT_COVER_W, RECENT_COVER_H);
      recentCoverPaths[index] = (!coverPath.empty() && Storage.exists(coverPath.c_str())) ? coverPath : "";
    }
  }
  loadedPageStart = pageStart;
}

void RecentBooksActivity::loop() {
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems =
      gridView ? RECENT_GRID_ITEMS : UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

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

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems = gridView ? RECENT_GRID_ITEMS : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
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
    const int gridWidth = RECENT_GRID_COLUMNS * RECENT_COVER_W + (RECENT_GRID_COLUMNS - 1) * RECENT_GRID_GAP;
    const int startX = (pageWidth - gridWidth) / 2;
    const int gridTop = contentTop + 56;
    const int rowGap = 4;

    const std::string title = renderer.truncatedText(UI_10_FONT_ID, recentTitle(recentBooks[selectorIndex]).c_str(),
                                                     pageWidth - metrics.contentSidePadding * 2, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 2, title.c_str(), true, EpdFontFamily::BOLD);
    const std::string subtitle = recentBooks[selectorIndex].author.empty() ? recentProgressLabels[selectorIndex]
                                                                           : recentBooks[selectorIndex].author;
    if (!subtitle.empty()) {
      const std::string safeSubtitle =
          renderer.truncatedText(SMALL_FONT_ID, subtitle.c_str(), pageWidth - metrics.contentSidePadding * 2);
      renderer.drawCenteredText(SMALL_FONT_ID, contentTop + 20, safeSubtitle.c_str(), true);
    }
    if (!recentProgressLabels[selectorIndex].empty() && !recentBooks[selectorIndex].author.empty()) {
      renderer.drawCenteredText(SMALL_FONT_ID, contentTop + 36, recentProgressLabels[selectorIndex].c_str(), true);
    }

    for (int index = pageStart; index < pageEnd; ++index) {
      const int local = index - pageStart;
      const int col = local % RECENT_GRID_COLUMNS;
      const int row = local / RECENT_GRID_COLUMNS;
      const Rect cover{startX + col * (RECENT_COVER_W + RECENT_GRID_GAP), gridTop + row * (RECENT_COVER_H + rowGap),
                       RECENT_COVER_W, RECENT_COVER_H};
      bool drawn = false;
      if (!recentCoverPaths[index].empty()) {
        FsFile file;
        if (Storage.openFileForRead("RBA", recentCoverPaths[index], file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
            renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height);
            drawn = true;
          }
          file.close();
        }
      }
      if (!drawn) drawRecentPlaceholder(renderer, cover);
      if (index == static_cast<int>(selectorIndex)) {
        renderer.drawRoundedRect(cover.x - 4, cover.y - 4, cover.width + 8, cover.height + 8, 2, 5, true);
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
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
