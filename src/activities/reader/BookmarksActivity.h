#pragma once

#include <Epub.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../UiListActivity.h"
#include "BookmarkStore.h"

// Fork: the reader's highlights / page-marks list. Tap (or Confirm) opens the
// bookmark; long-press (or a held Confirm) asks to delete it.
class BookmarksActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  std::vector<BookmarkStore::Bookmark> bookmarks;
  std::string headerTitleText;
  std::function<bool(const BookmarkStore::Bookmark&)> onDeleteBookmark;

  // Only materialize the viewport's rows: copying every full highlight into
  // a label crowds out the SD fallback glyph cache on the X4. Like the TOC,
  // keep a bounded window and batch-prewarm it when the viewport changes.
  static constexpr int ROW_WINDOW = 24;
  std::string rowLabels[ROW_WINDOW];
  freeink::ui::ListItem rowItems[ROW_WINDOW];
  int rowWindowStart = -1;
  int rowWindowCount = 0;
  void refreshRowWindow(int start);
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  std::string getItemLabel(int index) const;
  void confirmDeleteBookmark(int index);
  void finishCancelled();

  int listCount() const override { return static_cast<int>(bookmarks.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Confirm activates on RELEASE (a hold is "delete"); Back finishes cancelled.
  bool handleButtons() override;
  const char* headerTitle() const override;

 public:
  explicit BookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::vector<BookmarkStore::Bookmark>& bookmarks,
                             std::shared_ptr<Epub> epub = nullptr, std::string headerTitle = {},
                             std::function<bool(const BookmarkStore::Bookmark&)> onDeleteBookmark = nullptr)
      : UiListActivity("Bookmarks", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        epub(std::move(epub)),
        bookmarks(bookmarks),
        headerTitleText(std::move(headerTitle)),
        onDeleteBookmark(std::move(onDeleteBookmark)) {}

  void onEnter() override;
  void onExit() override;
};
