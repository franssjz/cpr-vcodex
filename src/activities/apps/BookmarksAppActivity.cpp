#include "BookmarksAppActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "../reader/BookmarksActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long DELETE_BOOKMARKS_HOLD_MS = 1000;

std::string getDisplayTitle(const ReadingBookStats& book) {
  if (!book.title.empty()) {
    return book.title;
  }

  const auto slashPos = book.path.find_last_of('/');
  if (slashPos == std::string::npos || slashPos + 1 >= book.path.size()) {
    return book.path;
  }
  return book.path.substr(slashPos + 1);
}
}  // namespace

void BookmarksAppActivity::refreshEntries() {
  entries.clear();

  for (const auto& book : READING_STATS.getBooks()) {
    if (book.bookId.empty() || !FsHelpers::hasEpubExtension(book.path) || !Storage.exists(book.path.c_str())) {
      continue;
    }

    BookmarkStore store;
    store.load("", book.bookId);
    if (store.isEmpty()) {
      continue;
    }

    entries.push_back(BookEntry{
        .bookId = book.bookId,
        .path = book.path,
        .title = getDisplayTitle(book),
        .author = book.author,
        .bookmarks = store.getAll(),
    });
  }

  if (selectedIndex >= static_cast<int>(entries.size())) {
    selectedIndex = std::max(0, static_cast<int>(entries.size()) - 1);
  }
}

void BookmarksAppActivity::openSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  const BookEntry entry = entries[selectedIndex];
  startActivityForResult(std::make_unique<BookmarksActivity>(
                             renderer, mappedInput, entry.bookmarks, nullptr, entry.title,
                             [path = entry.path, bookId = entry.bookId](const BookmarkStore::Bookmark& bookmark) {
                               BookmarkStore store;
                               store.load("", bookId);
                               const bool removed = store.remove(bookmark.spineIndex, bookmark.pageNumber);
                               if (removed) {
                                 store.save();
                               }
                               return removed;
                             }),
                         [this, path = entry.path](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto& bookmark = std::get<BookmarkResult>(result.data);
                             activityManager.goToEpubBookmark(path, bookmark.spineIndex, bookmark.page);
                             return;
                           }
                           refreshEntries();
                           requestUpdate();
                         });
}

bool BookmarksAppActivity::clearBookmarksForBook(const std::string&, const std::string& bookId) const {
  BookmarkStore store;
  store.load("", bookId);
  if (store.isEmpty()) {
    return true;
  }

  store.clear();
  store.save();
  return true;
}

void BookmarksAppActivity::confirmDeleteSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  const BookEntry entry = entries[selectedIndex];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ALL_BOOKMARKS), entry.title),
      [this, path = entry.path, bookId = entry.bookId](const ActivityResult& result) {
        if (!result.isCancelled) {
          clearBookmarksForBook(path, bookId);
          refreshEntries();
        }
        requestUpdate();
      });
}

void BookmarksAppActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();
  requestUpdate();
}

void BookmarksAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_BOOKMARKS_HOLD_MS) {
      confirmDeleteSelectedBook();
      return;
    }

    openSelectedBook();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void BookmarksAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_BOOKMARKS));

  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BOOKMARKS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, listHeight}, static_cast<int>(entries.size()), selectedIndex,
        [this](const int index) { return entries[index].title; },
        [this](const int index) {
          if (!entries[index].author.empty()) {
            return entries[index].author;
          }
          return entries[index].path;
        },
        [](const int) { return UIIcon::Book; },
        [this](const int index) { return std::to_string(entries[index].bookmarks.size()); });
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
