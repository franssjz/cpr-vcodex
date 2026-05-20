#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksGrid.h"

class RecentBooksActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  // Recent tab state
  std::vector<RecentBooksGrid::BookState> recentBooks;
  std::vector<uint8_t> recentBookCompletedStates;
  int loadedPageStart = -1;
  bool confirmLongPressHandled = false;
  bool holdPreviewVisible = false;

  // Data loading
  void loadRecentBooks();
  void loadVisiblePageMetadata(int pageItems);
  void requestRemoveRecentBook(size_t selectedIndex);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooks", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
