#pragma once

// Test entry points only. Include via navigation_fixture.patch in an isolated
// simulator checkout; this header is not part of device firmware.
#include <cstdlib>
#include <string>

#include "activities/apps/AppsActivity.h"
#include "activities/apps/BookStatsActionsActivity.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/ScreenCleanActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "activities/settings/KOReaderProfileListActivity.h"
#include "activities/settings/SettingsActivity.h"

inline void startNavigationFixture(GfxRenderer& renderer, MappedInputManager& input) {
  const std::string screen = std::getenv("CPR_NAV_SCREEN");
  if (screen == "apps") activityManager.replaceActivity(std::make_unique<AppsActivity>(renderer, input));
  if (screen == "bookstats") {
    activityManager.replaceActivity(
        std::make_unique<BookStatsActionsActivity>(renderer, input, "/test.epub", "Navigation test"));
  }
  if (screen == "sync") activityManager.replaceActivity(std::make_unique<SyncDayActivity>(renderer, input));
  if (screen == "favorites") activityManager.replaceActivity(std::make_unique<FavoritesAppActivity>(renderer, input));
  if (screen == "flashcards") activityManager.replaceActivity(std::make_unique<FlashcardsAppActivity>(renderer, input));
  if (screen == "dictionary") activityManager.replaceActivity(std::make_unique<DictionaryActivity>(renderer, input));
  if (screen == "stats") activityManager.replaceActivity(std::make_unique<ReadingStatsActivity>(renderer, input));
  if (screen == "clean") activityManager.replaceActivity(std::make_unique<ScreenCleanActivity>(renderer, input));
  if (screen == "profiles")
    activityManager.replaceActivity(std::make_unique<KOReaderProfileListActivity>(renderer, input));
  if (screen == "settings") activityManager.replaceActivity(std::make_unique<SettingsActivity>(renderer, input));
  if (screen == "reader-menu") {
    activityManager.replaceActivity(
        std::make_unique<EpubReaderMenuActivity>(renderer, input, "Navigation test", 2, 10, 20, 0, false));
  }
}
