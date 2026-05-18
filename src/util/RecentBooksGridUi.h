#pragma once

#include <Bitmap.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "util/ButtonNavigator.h"

namespace RecentBooksGridUi {
constexpr int kColumns = 3;
constexpr int kMainItemsPerPage = 9;
constexpr int kQuickItemsPerPage = 6;
constexpr int kCoverWidth = 123;
constexpr int kCoverHeight = 196;
constexpr int kCoverGap = 8;
constexpr int kRowGap = 10;
constexpr int kMetadataBandHeight = 42;
constexpr int kCoverCornerRadius = 2;

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
  const int row = indexInPage / kColumns;
  const int col = indexInPage % kColumns;
  const int rowsPerPage = safeItemsPerPage / kColumns;

  if (moveDown) {
    if (row < rowsPerPage - 1) {
      const int next = currentIndex + kColumns;
      if (next < totalItems && next / safeItemsPerPage == currentPage) {
        return next;
      }
    }

    const int nextPage = (currentPage + 1) % totalPages;
    const int nextStart = nextPage * safeItemsPerPage;
    const int nextCount = std::min(safeItemsPerPage, totalItems - nextStart);
    return nextStart + std::min(col, std::max(0, nextCount - 1));
  }

  if (row > 0) {
    return currentIndex - kColumns;
  }

  const int previousPage = (currentPage - 1 + totalPages) % totalPages;
  const int previousStart = previousPage * safeItemsPerPage;
  const int previousCount = std::min(safeItemsPerPage, totalItems - previousStart);
  int target = previousStart + ((std::max(1, previousCount) - 1) / kColumns) * kColumns + col;
  while (target >= previousStart + previousCount) {
    target -= kColumns;
  }
  return std::max(previousStart, target);
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
}  // namespace RecentBooksGridUi
