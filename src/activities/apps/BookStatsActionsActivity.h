#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class BookStatsActionsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string bookPath;
  std::string bookTitle;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool startDateApplyFailed = false;

  void openAdjustment();
  void openStartDateSelection();
  void confirmResetBookStats();
  void guardConfirmReturn();

 public:
  static constexpr int RESULT_RESET_BOOK_STATS = 1;

  explicit BookStatsActionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                    std::string bookTitle)
      : Activity("BookStatsActions", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
