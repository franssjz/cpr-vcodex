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
#include "util/RecentBooksGridUi.h"

namespace {
constexpr int MAX_QUICK_RECENT_BOOKS = 6;

std::string recentBookTitle(const RecentBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }
  const size_t slash = book.path.find_last_of('/');
  return slash == std::string::npos ? book.path : book.path.substr(slash + 1);
}

std::string recentBookProgress(const RecentBook& book) {
  const auto* stats = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
  return stats == nullptr ? "" : std::to_string(stats->lastProgressPercent) + "%";
}

}  // namespace

ReaderRecentBooksActivity::ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const std::string& currentPath)
    : Activity("ReaderRecentBooks", renderer, mappedInput), currentPath(currentPath) {}

void ReaderRecentBooksActivity::loadBooks() {
  books.clear();
  coverPaths.clear();
  coverPathResolvedStates.clear();
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
  coverPathResolvedStates.assign(books.size(), 0);
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
    moveSelection(RecentBooksGridUi::moveHorizontal(selectedIndex, total, true));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveHorizontal(selectedIndex, total, false));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveVertical(selectedIndex, total, RecentBooksGridUi::kQuickItemsPerPage, true));
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveVertical(selectedIndex, total, RecentBooksGridUi::kQuickItemsPerPage, false));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveHorizontal(selectedIndex, total, true));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveHorizontal(selectedIndex, total, false));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveVertical(selectedIndex, total, RecentBooksGridUi::kQuickItemsPerPage, true));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, total, moveSelection] {
    moveSelection(RecentBooksGridUi::moveVertical(selectedIndex, total, RecentBooksGridUi::kQuickItemsPerPage, false));
  });
}

void ReaderRecentBooksActivity::loadVisiblePageMetadata() {
  if (books.empty()) return;
  const int pageStart = (selectedIndex / RecentBooksGridUi::kQuickItemsPerPage) * RecentBooksGridUi::kQuickItemsPerPage;
  if (pageStart == loadedPageStart) return;
  const int pageEnd = std::min(pageStart + RecentBooksGridUi::kQuickItemsPerPage, static_cast<int>(books.size()));
  for (int index = pageStart; index < pageEnd; ++index) {
    if (progressLabels[index].empty()) {
      progressLabels[index] = recentBookProgress(books[index]);
    }
    if (index >= static_cast<int>(coverPathResolvedStates.size()) || coverPathResolvedStates[index] != 0) {
      continue;
    }
    const std::string coverPath =
        UITheme::resolveBookCoverThumbPath(books[index].path, books[index].coverBmpPath,
                                           RecentBooksGridUi::kCoverWidth, RecentBooksGridUi::kCoverHeight);
    coverPaths[index] = (!coverPath.empty() && Storage.exists(coverPath.c_str())) ? coverPath : "";
    coverPathResolvedStates[index] = 1;
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
    const int currentPage = selectedIndex / RecentBooksGridUi::kQuickItemsPerPage;
    const int pageStart = currentPage * RecentBooksGridUi::kQuickItemsPerPage;
    const int pageCount = std::min(RecentBooksGridUi::kQuickItemsPerPage, static_cast<int>(books.size()) - pageStart);
    const int gridWidth = RecentBooksGridUi::kColumns * RecentBooksGridUi::kCoverWidth +
                          (RecentBooksGridUi::kColumns - 1) * RecentBooksGridUi::kCoverGap;
    const int startX = (pageWidth - gridWidth) / 2;
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int metadataTop = contentTop;

    const std::string title = recentBookTitle(books[selectedIndex]);
    const std::string safeTitle =
        renderer.truncatedText(UI_10_FONT_ID, title.c_str(), pageWidth - metrics.contentSidePadding * 2,
                               EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, metadataTop + 2, safeTitle.c_str(), true, EpdFontFamily::BOLD);

    std::string detailLine = books[selectedIndex].author;
    if (!progressLabels[selectedIndex].empty()) {
      if (!detailLine.empty()) detailLine += "  |  ";
      detailLine += progressLabels[selectedIndex];
    }
    if (!detailLine.empty()) {
      const std::string safeDetail =
          renderer.truncatedText(SMALL_FONT_ID, detailLine.c_str(), pageWidth - metrics.contentSidePadding * 2);
      renderer.drawCenteredText(SMALL_FONT_ID, metadataTop + 22, safeDetail.c_str(), true);
    }

    const int gridTop = contentTop + RecentBooksGridUi::kMetadataBandHeight + RecentBooksGridUi::kCoverGap;
    renderer.drawLine(metrics.contentSidePadding, gridTop - 8, pageWidth - metrics.contentSidePadding, gridTop - 8,
                      true);
    for (int index = 0; index < pageCount; ++index) {
      const int bookIndex = pageStart + index;
      const int col = index % RecentBooksGridUi::kColumns;
      const int row = index / RecentBooksGridUi::kColumns;
      const Rect cover{startX + col * (RecentBooksGridUi::kCoverWidth + RecentBooksGridUi::kCoverGap),
                       gridTop + row * (RecentBooksGridUi::kCoverHeight + RecentBooksGridUi::kRowGap),
                       RecentBooksGridUi::kCoverWidth, RecentBooksGridUi::kCoverHeight};
      bool drawn = false;
      if (!coverPaths[bookIndex].empty()) {
        FsFile file;
        if (Storage.openFileForRead("RRBA", coverPaths[bookIndex], file)) {
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
      if (!drawn) {
        RecentBooksGridUi::drawPlaceholder(renderer, cover);
      }
      if (bookIndex == selectedIndex) {
        renderer.drawRoundedRect(cover.x - 4, cover.y - 4, cover.width + 8, cover.height + 8, 3,
                                 RecentBooksGridUi::kCoverCornerRadius + 4, true);
        renderer.drawRoundedRect(cover.x - 6, cover.y - 6, cover.width + 12, cover.height + 12, 1,
                                 RecentBooksGridUi::kCoverCornerRadius + 6, true);
      }
    }

    const int totalPages =
        (static_cast<int>(books.size()) + RecentBooksGridUi::kQuickItemsPerPage - 1) /
        RecentBooksGridUi::kQuickItemsPerPage;
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
