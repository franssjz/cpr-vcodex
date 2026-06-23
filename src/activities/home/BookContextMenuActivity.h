#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"

class BookContextMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    OPEN_BOOK,
    REMOVE_FROM_RECENTS,
    ADD_TO_FAVORITES,
    VIEW_METADATA,
    VIEW_STATS,
    MARK_READ_UNREAD,
    DELETE_CACHE
  };

  explicit BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::string& bookTitle, bool isFavorite, bool isCompleted,
                                   bool isEpubFormat);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool isFavorite, bool isCompleted, bool isEpubFormat);

  const std::vector<MenuItem> menuItems;
  const std::string bookTitle;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
};