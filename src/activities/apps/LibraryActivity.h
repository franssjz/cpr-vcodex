#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

struct LibraryEntry {
  std::string path;
  std::string title;
  std::string coverPath;
};

class LibraryActivity final : public Activity {
 private:
  int selectorIndex_ = 0;
  std::vector<LibraryEntry> entries_;
  int coverGenIndex_ = -1;
  bool coversComplete_ = false;

  int coverWidth_ = 100;
  int coverHeight_ = 150;
  int gridColumns_ = 4;
  int gridsPerPage_ = 16;

  void applyLayoutFromSettings();
  void scanSd();
  void generateCoverForEntry(int index);
  std::string libraryCoverPath(const std::string& bookPath) const;

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
};