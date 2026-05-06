#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "./FileBrowserActivity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  int lastCarouselBookIndex = 0;
  std::string carouselCoverLoadAttemptPath;
  uint8_t* carouselFrames[3] = {nullptr, nullptr, nullptr};
  int carouselFrameBookIdx[3] = {-1, -1, -1};
  bool carouselFramesReady = false;
  std::vector<RecentBook> recentBooks;
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onAppsOpen();
  void onReadingStatsOpen();
  void onSyncDayOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void preRenderCarouselFrames();
  void freeCarouselFrames();
  void renderCarouselFrame(int slot, int bookIndex);
  bool loadCarouselFrameFromStorage(int slot, int bookIndex);
  bool saveCarouselFrameToStorage(int bookIndex);
  void updateSlidingWindowCache(int centerIdx, int bookCount);
  void scheduleCarouselCoverLoadIfNeeded();
  void loadHomeCarouselBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  bool needsRecentCoverLoad(int coverHeight) const;

 public:
  static constexpr int kCarouselFrameCount = 3;

  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
