#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

struct Rect;

class FileBrowserActivity final : public Activity {
 private:
  // Deletion
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  uint8_t libraryView = 0;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::vector<uint8_t> completedFileStates;
  std::vector<uint8_t> progressFileStates;
  std::vector<uint8_t> libraryFileStates;
  std::vector<uint16_t> folderItemCounts;
  std::vector<std::string> entryPaths;
  std::vector<std::string> entryTitles;
  std::vector<std::string> entrySubtitles;
  std::vector<std::string> entryCoverPaths;

  // Data loading
  void loadFiles();
  void loadFilesystemFiles();
  void loadLibraryDashboard();
  void loadLibraryShelf(uint8_t shelf);
  void addLibraryBook(const std::string& path, const std::string& title, const std::string& author,
                      const std::string& coverPath, uint8_t progress, uint8_t state);
  bool isLibraryDashboard() const;
  bool isLibraryShelf() const;
  void clampSelector();
  size_t findEntry(const std::string& name) const;
  bool isBookshelfMode() const;
  int getBookshelfColumns() const;
  int getBookshelfCardHeight() const;
  int getPageItems(const int contentHeight) const;
  uint16_t countFolderItems(const std::string& folderName) const;
  std::string getFullPathForEntry(const std::string& entry) const;
  std::string getLibraryStateLabel(int index) const;
  std::string getEntryTitle(int index) const;
  std::string getEntrySubtitle(int index) const;
  void moveBookshelfHorizontal(int delta);
  void moveBookshelfVertical(int delta);
  void renderBookshelf(const Rect& rect, const int pageItems);
  void renderLibraryDashboard(const Rect& rect, const int pageItems);
  void renderPageIndicator(const Rect& rect, int pageItems) const;
  void openBookActions(size_t index);
  void handleBookAction(int action, const std::string& path, const std::string& title, const std::string& entry);
  void confirmDeleteFile(const std::string& fullPath, const std::string& label);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/")
      : Activity("FileBrowser", renderer, mappedInput), basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
