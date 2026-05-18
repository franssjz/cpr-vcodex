#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class RecentBooksActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  // Recent tab state
  std::vector<RecentBook> recentBooks;
  std::vector<uint8_t> recentBookCompletedStates;
  std::vector<std::string> recentCoverPaths;
  std::vector<uint8_t> recentCoverResolvedStates;
  std::vector<std::string> recentProgressLabels;
  int loadedPageStart = -1;

  // Data loading
  void loadRecentBooks();
  void loadVisiblePageMetadata(int pageItems);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooks", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
