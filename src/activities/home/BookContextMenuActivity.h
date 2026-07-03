#pragma once
#include <I18n.h>
#include <components/themes/BaseTheme.h>

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
    DELETE_CACHE,
    DELETE_COVER_THUMB,
    DELETE_PAGE_COVER_THUMBS,
    DELETE_ALL_LIBRARY_COVERS,
    REINDEX_LIBRARY,
    CLEAR_THEME_CACHE
  };

  explicit BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::string& bookTitle, bool isFavorite, bool isCompleted,
                                   bool isEpubFormat, bool isLibraryMode = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
    UIIcon icon;
  };

  static std::vector<MenuItem> buildMenuItems(bool isFavorite, bool isCompleted, bool isEpubFormat, bool isLibraryMode = false);

  const std::vector<MenuItem> menuItems;
  const std::string bookTitle;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
};