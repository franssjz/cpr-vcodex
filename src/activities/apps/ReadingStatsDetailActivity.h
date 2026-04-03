#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

struct ReadingStatsDetailContext {
  bool showSessionSummary = false;
};

class ReadingStatsDetailActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string bookPath;
  std::string resolvedCoverBmpPath;
  ReadingStatsDetailContext context;
  bool coverLoadPending = false;
  bool coverLoaded = false;

  void setCurrentBookByIndex(int index);
  void navigateBook(int direction);

 public:
  explicit ReadingStatsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                      ReadingStatsDetailContext context = {})
      : Activity("ReadingStatsDetail", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        context(std::move(context)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
