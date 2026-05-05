#pragma once

#include <cstdint>
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
  int selectedStatsItem = 0;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  bool baseScreenBufferStored = false;
  uint8_t* baseScreenBuffer = nullptr;
  std::string baseScreenBookPath;
  std::string baseScreenCoverPath;

  void openAdjustment();
  void guardChildReturn();
  bool storeBaseScreenBuffer();
  bool restoreBaseScreenBuffer();
  void invalidateBaseScreenBuffer();
  void freeBaseScreenBuffer();

 public:
  explicit ReadingStatsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                      ReadingStatsDetailContext context = {})
      : Activity("ReadingStatsDetail", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        context(std::move(context)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
