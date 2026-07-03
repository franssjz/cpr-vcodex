#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "CrossPointSettings.h"
#include "components/LibraryCache.h"
#include "components/LibraryPopupOverlay.h"
#include "util/ButtonNavigator.h"

class LibraryActivity final : public Activity {
 private:
  int selectorIndex_ = 0;
  std::vector<LibraryCache::Entry> entries_;
  std::vector<LibraryCache::Entry> unfilteredEntries_;
  int coverGenIndex_ = -1;
  bool coversComplete_ = false;
  uint64_t coverGeneratedMask_ = 0;  // bitmask of slots already generated this page pass
  int lastPage_ = -1;
  mutable int lastRenderedPage_ = -1;
  mutable int lastRenderedSelectorIndex_ = -1;
  mutable bool forceRender_ = true;

  int coverWidth_ = 100;
  int coverHeight_ = 150;
  int gridColumns_ = 4;
  int gridsPerPage_ = 16;
  int gap_ = 7;
  int rowPad_ = 8;
  CrossPointSettings::LIBRARY_FILTER currentFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
  CrossPointSettings::LIBRARY_SORT currentSort_ = CrossPointSettings::LIBRARY_SORT_TITLE_ASC;
  std::string currentSearchText_;
  uint8_t lastLayoutSetting_ = CrossPointSettings::LIBRARY_LAYOUT_4X4;

  // Popup state
  enum class PopupMode { None, Sort, Filter };
  PopupMode popupMode_ = PopupMode::None;
  LibraryPopupOverlay popupOverlay_;

  // Long-press tracking for Up/Down
  bool upHeld_ = false;
  bool upLongTriggered_ = false;
  bool downHeld_ = false;
  bool downLongTriggered_ = false;
  static constexpr unsigned long kLongPressMs = 1000;

  void applyLayoutFromSettings();
  void ensureLayoutUpToDate();
  void scanSd();
  void applyFilterAndSort();
  bool isBookCoverReady(const LibraryCache::Entry& entry) const;
  void deleteLibraryCovers(const std::string& bookPath);
  void deletePageCovers();
  void deleteAllLibraryCovers();
  void rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter);

  void openSortPopup();
  void openFilterPopup();
  void closePopup();
  void selectPopupItem();
  void beginTextSearch();

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
};