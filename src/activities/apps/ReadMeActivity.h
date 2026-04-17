#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ReadMeActivity final : public Activity {
 public:
  explicit ReadMeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadMe", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ViewMode : uint8_t { Menu = 0, Detail };
  enum class Topic : uint8_t {
    SyncDay = 0,
    Stats,
    ReadingProfile,
    Achievements,
    Bookmarks,
    Favorites,
    Flashcards,
    Sleep,
    SettingsGuide,
    Shortcuts,
    IfFound,
    Reports,
    Count
  };

  ButtonNavigator buttonNavigator;
  bool waitForConfirmRelease = false;
  ViewMode viewMode = ViewMode::Menu;
  int selectedIndex = 0;
  Topic activeTopic = Topic::SyncDay;
  int scrollOffset = 0;
  std::vector<std::string> detailLines;

  void openSelectedTopic();
  void loadDetailLines();
  int getVisibleDetailLineCount() const;
  int getMaxScrollOffset() const;
  static int getTopicCount();
  static std::string getTopicTitle(Topic topic);
  static std::string getTopicBody(Topic topic);
  static std::string getTopicIndexLabel(Topic topic);
};
