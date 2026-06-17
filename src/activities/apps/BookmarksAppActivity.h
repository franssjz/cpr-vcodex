#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../reader/BookmarkStore.h"
#include "util/ButtonNavigator.h"

class BookmarksAppActivity final : public Activity {
  struct BookEntry {
    std::string bookId;
    std::string path;
    std::string title;
    std::string author;
    std::vector<BookmarkStore::Bookmark> bookmarks;
  };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::vector<BookEntry> entries;

  void refreshEntries();
  void openSelectedBook();
  bool clearBookmarksForBook(const std::string& bookId) const;
  void confirmDeleteSelectedBook();

 public:
  explicit BookmarksAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BookmarksApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
