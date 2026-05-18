#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderBookInfoActivity final : public Activity {
 public:
  ReaderBookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path, std::string title,
                         std::string author = "", std::string language = "", std::string currentChapter = "");

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct BookInfoView {
    std::string title;
    std::string author;
    std::string series;
    std::string seriesIndex;
    std::string tags;
    std::string publisher;
    std::string language;
    std::string description;
    std::string identifier;
    std::string currentChapter;
    std::string progress;
    std::string totalRead;
    std::string timeLeft;
    std::string lastOpened;
  };

  std::string path;
  std::string title;
  std::string author;
  std::string language;
  std::string currentChapter;
  BookInfoView info;
  std::string statusMessage;
  int scrollOffset = 0;
  int maxScrollOffset = 0;
  bool waitForBackRelease = false;
  ButtonNavigator buttonNavigator;

  void loadInfo();
  void fetchMetadata();
};
