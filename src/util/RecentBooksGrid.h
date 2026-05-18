#pragma once

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

namespace RecentBooksGrid {
constexpr int kColumns = 3;
constexpr int kItemsPerPage = 9;
constexpr int kQuickItemsPerPage = 6;
constexpr int kCoverWidth = 123;
constexpr int kCoverHeight = 196;
constexpr int kGridSpacing = 8;
constexpr int kRowSpacing = 10;
constexpr int kTitleStripHeight = 42;
constexpr int kTitleGridGap = 8;
constexpr int kCoverCornerRadius = 2;
constexpr int kSelectionPadding = 4;
constexpr int kSelectionOutlineGap = 2;

struct BookState {
  RecentBook book;
  std::string coverPath;
  std::string progressLabel;
  bool progressLoaded = false;
};

inline std::string titleFor(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const std::string name = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

inline std::string progressFor(const RecentBook& book) {
  const auto* stats = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
  return stats == nullptr ? "" : std::to_string(stats->lastProgressPercent) + "%";
}

inline void updateRecentBookCoverPath(const RecentBook& book, const std::string& coverBmpPath) {
  RECENT_BOOKS.updateBook(book.path, book.title, book.author, coverBmpPath, book.bookId);
}

inline bool hasThumbnailPlaceholder(const std::string& coverBmpPath) {
  return coverBmpPath.find("[WIDTH]") != std::string::npos || coverBmpPath.find("[HEIGHT]") != std::string::npos;
}

inline std::string getReusableCoverPath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, "/.crosspoint").getThumbBmpPath();
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return Xtc(book.path, "/.crosspoint").getThumbBmpPath();
  }
  return book.coverBmpPath;
}

inline void ensureReusableCoverPath(RecentBook& book) {
  if (book.coverBmpPath.empty() || hasThumbnailPlaceholder(book.coverBmpPath)) {
    return;
  }

  const std::string reusablePath = getReusableCoverPath(book);
  if (reusablePath.empty() || reusablePath == book.coverBmpPath) {
    return;
  }

  book.coverBmpPath = reusablePath;
  updateRecentBookCoverPath(book, reusablePath);
}

inline bool needsCoverThumbGeneration(const RecentBook& book, const std::string& thumbPath) {
  if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) {
    return true;
  }
  if (!FsHelpers::hasXtcExtension(book.path)) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("RBG", thumbPath, file)) {
    return true;
  }
  Bitmap bitmap(file);
  const bool hasExpectedSize =
      bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == kCoverWidth && bitmap.getHeight() == kCoverHeight;
  file.close();
  return !hasExpectedSize;
}

inline int moveHorizontal(const int currentIndex, const int totalItems, const bool moveRight) {
  if (totalItems <= 0) return 0;
  return moveRight ? ButtonNavigator::nextIndex(currentIndex, totalItems)
                   : ButtonNavigator::previousIndex(currentIndex, totalItems);
}

inline int moveVertical(const int currentIndex, const int totalItems, const int itemsPerPage, const bool moveDown) {
  if (totalItems <= 0) return 0;
  const int safeItemsPerPage = std::max(kColumns, itemsPerPage);
  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  const int currentPage = currentIndex / safeItemsPerPage;
  const int indexInPage = currentIndex % safeItemsPerPage;
  const int currentRow = indexInPage / kColumns;
  const int currentColumn = indexInPage % kColumns;
  const int rowsPerPage = safeItemsPerPage / kColumns;

  if (moveDown) {
    if (currentRow < rowsPerPage - 1) {
      const int nextRowCandidate = currentIndex + kColumns;
      if (nextRowCandidate < totalItems && nextRowCandidate / safeItemsPerPage == currentPage) {
        return nextRowCandidate;
      }
    }

    const int nextPage = (currentPage + 1) % totalPages;
    const int nextPageStart = nextPage * safeItemsPerPage;
    const int nextPageCount = std::min(safeItemsPerPage, totalItems - nextPageStart);
    if (nextPageCount <= 0) return currentIndex;
    return nextPageStart + std::min(currentColumn, nextPageCount - 1);
  }

  if (currentRow > 0) {
    return currentIndex - kColumns;
  }

  const int previousPage = (currentPage - 1 + totalPages) % totalPages;
  const int previousPageStart = previousPage * safeItemsPerPage;
  const int previousPageCount = std::min(safeItemsPerPage, totalItems - previousPageStart);
  if (previousPageCount <= 0) return currentIndex;

  int previousPageCandidate = previousPageStart + ((previousPageCount - 1) / kColumns) * kColumns + currentColumn;
  while (previousPageCandidate >= previousPageStart + previousPageCount) {
    previousPageCandidate -= kColumns;
  }
  return std::max(previousPageStart, previousPageCandidate);
}

inline void calculateCoverFillCrop(const Bitmap& bitmap, float& cropX, float& cropY) {
  cropX = 0.0f;
  cropY = 0.0f;
  const float srcW = static_cast<float>(bitmap.getWidth());
  const float srcH = static_cast<float>(bitmap.getHeight());
  if (srcW <= 0.0f || srcH <= 0.0f) return;

  const float srcRatio = srcW / srcH;
  const float targetRatio = static_cast<float>(kCoverWidth) / static_cast<float>(kCoverHeight);
  if (srcRatio > targetRatio) {
    cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
  } else if (srcRatio < targetRatio) {
    cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
  }
}

inline void drawPlaceholder(GfxRenderer& renderer, const Rect& rect) {
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kCoverCornerRadius, Color::White);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, kCoverCornerRadius, true);
  renderer.drawIcon(BookIcon, rect.x + (rect.width - 32) / 2, rect.y + (rect.height - 32) / 2, 32, 32);
}

inline void ensurePageProgress(std::vector<BookState>& books, const int pageStart, const int itemsPerPage) {
  if (pageStart < 0 || itemsPerPage <= 0) return;
  const int pageEnd = std::min(pageStart + itemsPerPage, static_cast<int>(books.size()));
  for (int index = pageStart; index < pageEnd; ++index) {
    BookState& state = books[index];
    if (!state.progressLoaded) {
      state.progressLabel = progressFor(state.book);
      state.progressLoaded = true;
    }
  }
}

inline bool loadPageCovers(GfxRenderer& renderer, std::vector<BookState>& books, const int pageStart,
                           const int itemsPerPage) {
  if (pageStart < 0 || itemsPerPage <= 0) return false;
  const int pageEnd = std::min(pageStart + itemsPerPage, static_cast<int>(books.size()));

  bool needsGeneration = false;
  for (int index = pageStart; index < pageEnd; ++index) {
    RecentBook& book = books[index].book;
    ensureReusableCoverPath(book);
    if (book.coverBmpPath.empty()) {
      books[index].coverPath = "";
      continue;
    }
    const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCoverWidth, kCoverHeight);
    books[index].coverPath = (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) ? thumbPath : "";
    if (needsCoverThumbGeneration(book, thumbPath)) {
      needsGeneration = true;
    }
  }
  if (!needsGeneration) {
    return false;
  }

  bool showingLoading = false;
  Rect popupRect;
  const int totalToProcess = std::max(1, pageEnd - pageStart);
  int processedCount = 0;

  for (int index = pageStart; index < pageEnd; ++index) {
    RecentBook& book = books[index].book;
    const std::string coverPath =
        book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, kCoverWidth, kCoverHeight);
    if (needsCoverThumbGeneration(book, coverPath)) {
      if (FsHelpers::hasEpubExtension(book.path)) {
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(false, true)) {
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + (processedCount * 90) / totalToProcess);
          if (epub.generateThumbBmp(kCoverWidth, kCoverHeight)) {
            const std::string reusablePath = epub.getThumbBmpPath();
            book.coverBmpPath = reusablePath;
            updateRecentBookCoverPath(book, reusablePath);
            books[index].coverPath = UITheme::getCoverThumbPath(reusablePath, kCoverWidth, kCoverHeight);
          } else {
            updateRecentBookCoverPath(book, "");
            book.coverBmpPath = "";
            books[index].coverPath = "";
          }
        }
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) {
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + (processedCount * 90) / totalToProcess);
          if (xtc.generateThumbBmp(kCoverWidth, kCoverHeight)) {
            const std::string reusablePath = xtc.getThumbBmpPath();
            book.coverBmpPath = reusablePath;
            updateRecentBookCoverPath(book, reusablePath);
            books[index].coverPath = UITheme::getCoverThumbPath(reusablePath, kCoverWidth, kCoverHeight);
          } else {
            updateRecentBookCoverPath(book, "");
            book.coverBmpPath = "";
            books[index].coverPath = "";
          }
        }
      }
    }
    processedCount++;
  }

  return showingLoading;
}

inline void drawSelectedTitle(GfxRenderer& renderer, const std::vector<BookState>& books, const int selectedIndex,
                              const int x, const int y, const int width) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) return;
  const BookState& selected = books[selectedIndex];
  const std::string title =
      renderer.truncatedText(UI_10_FONT_ID, titleFor(selected.book).c_str(), width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, y + 2, title.c_str(), true, EpdFontFamily::BOLD);

  std::string detailLine = selected.book.author;
  if (!selected.progressLabel.empty()) {
    if (!detailLine.empty()) detailLine += "  |  ";
    detailLine += selected.progressLabel;
  }
  if (!detailLine.empty()) {
    const std::string safeDetail = renderer.truncatedText(SMALL_FONT_ID, detailLine.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x, y + 22, safeDetail.c_str(), true);
  }
}

inline void drawPageDots(GfxRenderer& renderer, const int pageWidth, const int y, const int totalPages,
                         const int currentPage) {
  if (totalPages <= 1) return;
  constexpr int dotSize = 8;
  constexpr int dotSpacing = 6;
  const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
  int dotX = (pageWidth - totalDotWidth) / 2;
  for (int page = 0; page < totalPages; ++page) {
    if (page == currentPage) {
      renderer.fillRect(dotX, y, dotSize, dotSize, true);
    } else {
      renderer.drawRect(dotX, y, dotSize, dotSize, true);
    }
    dotX += dotSize + dotSpacing;
  }
}

inline void drawGrid(GfxRenderer& renderer, const std::vector<BookState>& books, const int selectedIndex,
                     const int pageStart, const int pageCount, const int startX, const int gridTop) {
  for (int index = 0; index < pageCount; ++index) {
    const int bookIndex = pageStart + index;
    const int col = index % kColumns;
    const int row = index / kColumns;
    const Rect cover{startX + col * (kCoverWidth + kGridSpacing), gridTop + row * (kCoverHeight + kRowSpacing),
                     kCoverWidth, kCoverHeight};
    bool drawn = false;
    std::string thumbPath = books[bookIndex].coverPath;
    if (thumbPath.empty() && !books[bookIndex].book.coverBmpPath.empty()) {
      thumbPath = UITheme::getCoverThumbPath(books[bookIndex].book.coverBmpPath, kCoverWidth, kCoverHeight);
    }
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      FsFile file;
      if (Storage.openFileForRead("RBG", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          float cropX = 0.0f;
          float cropY = 0.0f;
          calculateCoverFillCrop(bitmap, cropX, cropY);
          renderer.fillRoundedRect(cover.x, cover.y, cover.width, cover.height, kCoverCornerRadius, Color::White);
          renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(cover.x, cover.y, cover.width, cover.height, kCoverCornerRadius,
                                                 Color::White);
          renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 2, kCoverCornerRadius, true);
          drawn = true;
        }
        file.close();
      }
    }
    if (!drawn) drawPlaceholder(renderer, cover);
    if (bookIndex == selectedIndex) {
      const int selectionOuterInset = kSelectionPadding + kSelectionOutlineGap;
      renderer.drawRoundedRect(cover.x - kSelectionPadding, cover.y - kSelectionPadding,
                               cover.width + kSelectionPadding * 2, cover.height + kSelectionPadding * 2, 3,
                               kCoverCornerRadius + kSelectionPadding, true);
      renderer.drawRoundedRect(cover.x - selectionOuterInset, cover.y - selectionOuterInset,
                               cover.width + selectionOuterInset * 2, cover.height + selectionOuterInset * 2, 1,
                               kCoverCornerRadius + selectionOuterInset, true);
    }
  }
}

}  // namespace RecentBooksGrid
