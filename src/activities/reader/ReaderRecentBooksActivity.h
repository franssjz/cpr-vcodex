#pragma once

#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksGrid.h"

class ReaderRecentBooksActivity final : public Activity {
 public:
  explicit ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::string& currentPath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::string currentPath;
  std::vector<RecentBooksGrid::BookState> books;
  int selectedIndex = 0;
  int loadedPageStart = -1;
  bool waitForInputRelease = false;
  ButtonNavigator buttonNavigator;

  void loadBooks();
  void loadVisiblePageMetadata();
};
