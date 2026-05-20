#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderNavigationMenuActivity final : public Activity {
 public:
  enum class Action { OPEN_RECENT_BOOKS = 0 };

  explicit ReaderNavigationMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const std::string& title);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct Item {
    Action action;
    StrId labelId;
  };

  std::string title;
  std::vector<Item> items;
  int selectedIndex = 0;
  bool waitForBackRelease = false;
  ButtonNavigator buttonNavigator;
};
