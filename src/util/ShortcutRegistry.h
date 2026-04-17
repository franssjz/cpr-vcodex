#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "components/themes/BaseTheme.h"

enum class ShortcutId {
  BrowseFiles = 0,
  Stats,
  SyncDay,
  Settings,
  ReadingStats,
  ReadingHeatmap,
  ReadingProfile,
  Achievements,
  IfFound,
  ReadMe,
  RecentBooks,
  Bookmarks,
  Favorites,
  Flashcards,
  FileTransfer,
  Sleep,
};

struct ShortcutDefinition {
  ShortcutId id;
  StrId nameId;
  StrId descriptionId;
  UIIcon icon;
  uint8_t CrossPointSettings::* locationPtr;
  uint8_t CrossPointSettings::* orderPtr;
  uint8_t CrossPointSettings::* visiblePtr;
};

inline const std::array<ShortcutDefinition, 16>& getShortcutDefinitions() {
  static const std::array<ShortcutDefinition, 16> definitions = {
      ShortcutDefinition{ShortcutId::BrowseFiles, StrId::STR_BROWSE_FILES, StrId::STR_NONE_OPT, UIIcon::Folder,
                         &CrossPointSettings::browseFilesShortcut, &CrossPointSettings::browseFilesShortcutOrder,
                         &CrossPointSettings::browseFilesShortcutVisible},
      ShortcutDefinition{ShortcutId::Stats, StrId::STR_STATS_SHORTCUT, StrId::STR_NONE_OPT, UIIcon::Book,
                         &CrossPointSettings::statsShortcut, &CrossPointSettings::statsShortcutOrder,
                         &CrossPointSettings::statsShortcutVisible},
      ShortcutDefinition{ShortcutId::SyncDay, StrId::STR_SYNC_DAY, StrId::STR_SYNC_DAY_DESC, UIIcon::Wifi,
                         &CrossPointSettings::syncDayShortcut, &CrossPointSettings::syncDayShortcutOrder,
                         &CrossPointSettings::syncDayShortcutVisible},
      ShortcutDefinition{ShortcutId::Settings, StrId::STR_SETTINGS_TITLE, StrId::STR_SETTINGS_APP_DESC,
                         UIIcon::Settings, &CrossPointSettings::settingsShortcut,
                         &CrossPointSettings::settingsShortcutOrder, &CrossPointSettings::settingsShortcutVisible},
      ShortcutDefinition{ShortcutId::ReadingStats, StrId::STR_READING_STATS, StrId::STR_READING_STATS_DESC,
                         UIIcon::Book, &CrossPointSettings::readingStatsShortcut,
                         &CrossPointSettings::readingStatsShortcutOrder, &CrossPointSettings::readingStatsShortcutVisible},
      ShortcutDefinition{ShortcutId::ReadingHeatmap, StrId::STR_READING_HEATMAP, StrId::STR_READING_HEATMAP_DESC,
                         UIIcon::Library, &CrossPointSettings::readingHeatmapShortcut,
                         &CrossPointSettings::readingHeatmapShortcutOrder,
                         &CrossPointSettings::readingHeatmapShortcutVisible},
      ShortcutDefinition{ShortcutId::ReadingProfile, StrId::STR_READING_PROFILE, StrId::STR_READING_PROFILE_DESC,
                         UIIcon::Library, &CrossPointSettings::readingProfileShortcut,
                         &CrossPointSettings::readingProfileShortcutOrder,
                         &CrossPointSettings::readingProfileShortcutVisible},
      ShortcutDefinition{ShortcutId::Achievements, StrId::STR_ACHIEVEMENTS, StrId::STR_ACHIEVEMENTS_APP_DESC,
                         UIIcon::Trophy, &CrossPointSettings::achievementsShortcut,
                         &CrossPointSettings::achievementsShortcutOrder, &CrossPointSettings::achievementsShortcutVisible},
      ShortcutDefinition{ShortcutId::IfFound, StrId::STR_IF_FOUND_RETURN_ME, StrId::STR_IF_FOUND_APP_DESC, UIIcon::File,
                         &CrossPointSettings::ifFoundShortcut, &CrossPointSettings::ifFoundShortcutOrder,
                         &CrossPointSettings::ifFoundShortcutVisible},
      ShortcutDefinition{ShortcutId::ReadMe, StrId::STR_README, StrId::STR_README_APP_DESC, UIIcon::Text,
                         &CrossPointSettings::readMeShortcut, &CrossPointSettings::readMeShortcutOrder,
                         &CrossPointSettings::readMeShortcutVisible},
      ShortcutDefinition{ShortcutId::RecentBooks, StrId::STR_MENU_RECENT_BOOKS, StrId::STR_RECENT_BOOKS_APP_DESC,
                         UIIcon::Recent, &CrossPointSettings::recentBooksShortcut,
                         &CrossPointSettings::recentBooksShortcutOrder, &CrossPointSettings::recentBooksShortcutVisible},
      ShortcutDefinition{ShortcutId::Bookmarks, StrId::STR_BOOKMARKS, StrId::STR_BOOKMARKS_APP_DESC, UIIcon::Book,
                         &CrossPointSettings::bookmarksShortcut, &CrossPointSettings::bookmarksShortcutOrder,
                         &CrossPointSettings::bookmarksShortcutVisible},
      ShortcutDefinition{ShortcutId::Favorites, StrId::STR_FAVORITES, StrId::STR_FAVORITES_APP_DESC, UIIcon::Heart,
                         &CrossPointSettings::favoritesShortcut, &CrossPointSettings::favoritesShortcutOrder,
                         &CrossPointSettings::favoritesShortcutVisible},
      ShortcutDefinition{ShortcutId::Flashcards, StrId::STR_FLASHCARDS, StrId::STR_FLASHCARDS_APP_DESC, UIIcon::Text,
                         &CrossPointSettings::flashcardsShortcut, &CrossPointSettings::flashcardsShortcutOrder,
                         &CrossPointSettings::flashcardsShortcutVisible},
      ShortcutDefinition{ShortcutId::FileTransfer, StrId::STR_FILE_TRANSFER, StrId::STR_FILE_TRANSFER_APP_DESC,
                         UIIcon::Transfer, &CrossPointSettings::fileTransferShortcut,
                         &CrossPointSettings::fileTransferShortcutOrder, &CrossPointSettings::fileTransferShortcutVisible},
      ShortcutDefinition{ShortcutId::Sleep, StrId::STR_SLEEP, StrId::STR_SLEEP_APP_DESC, UIIcon::Folder,
                         &CrossPointSettings::sleepShortcut, &CrossPointSettings::sleepShortcutOrder,
                         &CrossPointSettings::sleepShortcutVisible},
  };

  return definitions;
}

enum class ShortcutOrderGroup { Home = 0, Apps };

struct ShortcutOrderEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
};

inline const ShortcutDefinition* findShortcutDefinition(const ShortcutId id) {
  for (const auto& definition : getShortcutDefinitions()) {
    if (definition.id == id) {
      return &definition;
    }
  }
  return nullptr;
}

inline bool isShortcutAlwaysVisible(const ShortcutDefinition& definition) {
  return definition.id == ShortcutId::Settings;
}

inline uint8_t getShortcutOrder(const ShortcutDefinition& definition, const CrossPointSettings& settings = SETTINGS) {
  return settings.*(definition.orderPtr);
}

inline uint8_t& getShortcutOrderRef(CrossPointSettings& settings, const ShortcutDefinition& definition) {
  return settings.*(definition.orderPtr);
}

inline uint8_t& getShortcutOrderRef(CrossPointSettings& settings, const ShortcutOrderEntry& entry) {
  return entry.isAppsHub ? settings.appsHubShortcutOrder : settings.*(entry.definition->orderPtr);
}

inline bool getShortcutVisibility(const ShortcutDefinition& definition, const CrossPointSettings& settings = SETTINGS) {
  if (isShortcutAlwaysVisible(definition)) {
    return true;
  }
  return settings.*(definition.visiblePtr) != 0;
}

inline uint8_t& getShortcutVisibilityRef(CrossPointSettings& settings, const ShortcutDefinition& definition) {
  return settings.*(definition.visiblePtr);
}

inline void normalizeShortcutOrderSettings(CrossPointSettings& settings) {
  struct OrderSlot {
    int stableIndex;
    uint8_t* value;
  };

  std::vector<OrderSlot> slots;
  slots.reserve(getShortcutDefinitions().size() + 1);
  slots.push_back(OrderSlot{0, &settings.appsHubShortcutOrder});

  int stableIndex = 1;
  for (const auto& definition : getShortcutDefinitions()) {
    slots.push_back(OrderSlot{stableIndex++, &(settings.*(definition.orderPtr))});
  }

  std::stable_sort(slots.begin(), slots.end(), [](const OrderSlot& lhs, const OrderSlot& rhs) {
    if (*lhs.value != *rhs.value) {
      return *lhs.value < *rhs.value;
    }
    return lhs.stableIndex < rhs.stableIndex;
  });

  for (size_t index = 0; index < slots.size(); ++index) {
    *slots[index].value = static_cast<uint8_t>(index);
  }
}

inline std::vector<const ShortcutDefinition*> getConfiguredShortcuts(
    const CrossPointSettings::SHORTCUT_LOCATION location) {
  std::vector<const ShortcutDefinition*> shortcuts;
  for (const auto& definition : getShortcutDefinitions()) {
    if (static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr)) == location &&
        getShortcutVisibility(definition)) {
      shortcuts.push_back(&definition);
    }
  }
  std::stable_sort(shortcuts.begin(), shortcuts.end(), [](const ShortcutDefinition* lhs, const ShortcutDefinition* rhs) {
    return getShortcutOrder(*lhs) < getShortcutOrder(*rhs);
  });
  return shortcuts;
}

inline std::vector<ShortcutOrderEntry> getShortcutOrderEntries(const ShortcutOrderGroup group) {
  std::vector<ShortcutOrderEntry> entries;
  if (group == ShortcutOrderGroup::Home) {
    entries.push_back(ShortcutOrderEntry{nullptr, true});
  }

  for (const auto& definition : getShortcutDefinitions()) {
    const auto location = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr));
    if ((group == ShortcutOrderGroup::Home && location == CrossPointSettings::SHORTCUT_HOME) ||
        (group == ShortcutOrderGroup::Apps && location == CrossPointSettings::SHORTCUT_APPS)) {
      entries.push_back(ShortcutOrderEntry{&definition, false});
    }
  }

  std::stable_sort(entries.begin(), entries.end(), [](const ShortcutOrderEntry& lhs, const ShortcutOrderEntry& rhs) {
    const uint8_t lhsOrder = lhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*lhs.definition);
    const uint8_t rhsOrder = rhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*rhs.definition);
    return lhsOrder < rhsOrder;
  });
  return entries;
}
