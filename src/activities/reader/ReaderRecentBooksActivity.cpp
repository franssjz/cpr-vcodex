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

namespace {
constexpr int MAX_QUICK_RECENT_BOOKS = 6;
constexpr int GRID_COLUMNS = 3;
constexpr int BOOKS_PER_PAGE = 6;
constexpr int COVER_WIDTH = 120;
constexpr int COVER_HEIGHT = 176;
constexpr int COVER_GAP = 8;

std::string recentBookTitle(const RecentBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }
  const size_t slash = book.path.find_last_of('/');
  return slash == std::string::npos ? book.path : book.path.substr(slash + 1);
}

std::string recentBookSubtitle(const RecentBook& book) {
  const auto* stats = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
  std::string subtitle = book.author;
  if (stats != nullptr) {
    const std::string progress = std::to_string(stats->lastProgressPercent) + "%";
    if (!subtitle.empty()) {
      subtitle += "  ";
    }
    subtitle += progress;
  }
  return subtitle;
}

std::string recentBookProgress(const RecentBook& book) {
  const auto* stats = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
  return stats == nullptr ? "" : std::to_string(stats->lastProgressPercent) + "%";
}

void drawFallbackBook(const GfxRenderer& renderer, const Rect& rect) {
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
  renderer.drawLine(bookX + bookW - 5, bookY + 6, bookX + bookW - 5, bookY + bookH - 7, true);
}

int moveVerticalInGrid(const int currentIndex, const int totalItems, const bool down) {
  if (totalItems <= 0) return 0;
  const int page = currentIndex / BOOKS_PER_PAGE;
  const int pageStart = page * BOOKS_PER_PAGE;
  const int indexInPage = currentIndex - pageStart;
  const int column = indexInPage % GRID_COLUMNS;
  int next = currentIndex + (down ? GRID_COLUMNS : -GRID_COLUMNS);
  if (next >= pageStart && next < std::min(totalItems, pageStart + BOOKS_PER_PAGE)) {
    return next;
  }
  const int totalPages = (totalItems + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
  const int targetPage = down ? (page + 1) % totalPages : (page - 1 + totalPages) % totalPages;
  const int targetStart = targetPage * BOOKS_PER_PAGE;
  const int targetCount = std::min(BOOKS_PER_PAGE, totalItems - targetStart);
  if (targetCount <= 0) return currentIndex;
  int target = targetStart + column;
  while (target >= targetStart + targetCount) target -= GRID_COLUMNS;
  return std::max(targetStart, target);
}
}  // namespace

ReaderRecentBooksActivity::ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const std::string& currentPath)
    : Activity("ReaderRecentBooks", renderer, mappedInput), currentPath(currentPath) {}

void ReaderRecentBooksActivity::loadBooks() {
  books.clear();
  coverPaths.clear();
  progressLabels.clear();
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
    books.push_back(resolvedBook);
    if (books.size() >= MAX_QUICK_RECENT_BOOKS) {
      break;
    }
  }
  coverPaths.assign(books.size(), "");
  progressLabels.assign(books.size(), "");
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
    setResult(KeyboardResult{books[selectedIndex].path});
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
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    moveSelection(selectedIndex);
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    moveSelection(selectedIndex);
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, total, moveSelection] {
    moveSelection(moveVerticalInGrid(selectedIndex, total, true));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, total, moveSelection] {
    moveSelection(moveVerticalInGrid(selectedIndex, total, false));
  });
}

void ReaderRecentBooksActivity::loadVisiblePageMetadata() {
  if (books.empty()) return;
  const int pageStart = (selectedIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
  if (pageStart == loadedPageStart) return;
  const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, static_cast<int>(books.size()));
  for (int index = pageStart; index < pageEnd; ++index) {
    progressLabels[index] = recentBookProgress(books[index]);
    if (!books[index].coverBmpPath.empty()) {
      const std::string coverPath = UITheme::resolveCoverThumbPath(books[index].coverBmpPath, COVER_WIDTH, COVER_HEIGHT);
      coverPaths[index] = (!coverPath.empty() && Storage.exists(coverPath.c_str())) ? coverPath : "";
    }
  }
  loadedPageStart = pageStart;
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
    const int currentPage = selectedIndex / BOOKS_PER_PAGE;
    const int pageStart = currentPage * BOOKS_PER_PAGE;
    const int pageCount = std::min(BOOKS_PER_PAGE, static_cast<int>(books.size()) - pageStart);
    const int gridWidth = GRID_COLUMNS * COVER_WIDTH + (GRID_COLUMNS - 1) * COVER_GAP;
    const int startX = (pageWidth - gridWidth) / 2;
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int titleTop = contentTop + 2;

    const std::string title = recentBookTitle(books[selectedIndex]);
    const std::string subtitle = recentBookSubtitle(books[selectedIndex]);
    const std::string safeTitle =
        renderer.truncatedText(UI_10_FONT_ID, title.c_str(), pageWidth - metrics.contentSidePadding * 2,
                               EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, titleTop, safeTitle.c_str(), true, EpdFontFamily::BOLD);
    if (!subtitle.empty()) {
      const std::string safeSubtitle =
          renderer.truncatedText(SMALL_FONT_ID, subtitle.c_str(), pageWidth - metrics.contentSidePadding * 2);
      renderer.drawCenteredText(SMALL_FONT_ID, titleTop + renderer.getLineHeight(UI_10_FONT_ID) + 2,
                                safeSubtitle.c_str(), true);
    }

    const int gridTop = contentTop + 56;
    const int rowGap = 4;
    for (int index = 0; index < pageCount; ++index) {
      const int bookIndex = pageStart + index;
      const int col = index % GRID_COLUMNS;
      const int row = index / GRID_COLUMNS;
      const Rect cover{startX + col * (COVER_WIDTH + COVER_GAP), gridTop + row * (COVER_HEIGHT + rowGap), COVER_WIDTH,
                       COVER_HEIGHT};
      bool drawn = false;
      if (!coverPaths[bookIndex].empty()) {
        FsFile file;
        if (Storage.openFileForRead("RRBA", coverPaths[bookIndex], file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
            renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height);
            drawn = true;
          }
          file.close();
        }
      }
      if (!drawn) {
        drawFallbackBook(renderer, cover);
      }
      if (bookIndex == selectedIndex) {
        renderer.drawRoundedRect(cover.x - 4, cover.y - 4, cover.width + 8, cover.height + 8, 2, 5, true);
      }
    }

    const int totalPages = (static_cast<int>(books.size()) + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
    if (totalPages > 1) {
      constexpr int dotSize = 7;
      constexpr int dotGap = 6;
      const int totalW = totalPages * dotSize + (totalPages - 1) * dotGap;
      int dotX = (pageWidth - totalW) / 2;
      const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 10;
      for (int page = 0; page < totalPages; ++page) {
        if (page == currentPage) {
          renderer.fillRect(dotX, dotY, dotSize, dotSize, true);
        } else {
          renderer.drawRect(dotX, dotY, dotSize, dotSize, true);
        }
        dotX += dotSize + dotGap;
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
