#include "util/ShortcutUiMetadata.h"

#include <I18n.h>
#include <WiFi.h>

#include <algorithm>

#include "AchievementsStore.h"
#include "FavoritesStore.h"
#include "FlashcardsStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "util/SleepImageUtils.h"
#include "util/TimeUtils.h"

namespace {
bool hasFlashcardStatsToShow(const FlashcardDeckRecord& record) {
  return record.sessionCount > 0 || record.seenCards > 0 || record.totalReviewed > 0 || record.totalCorrect > 0 ||
         record.totalWrong > 0 || record.totalSkipped > 0 || record.lastReviewedAt > 0;
}

std::string formatDurationHmCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string getStatsShortcutSubtitle() {
  const std::string todayValue = formatDurationHmCompact(READING_STATS.getTodayReadingMs());
  const std::string goalValue = formatDurationHmCompact(getDailyReadingGoalMs());
  return todayValue + " / " + goalValue + " | " + std::to_string(READING_STATS.getCurrentStreakDays());
}

std::string getAchievementsShortcutSubtitle() {
  const auto views = ACHIEVEMENTS.buildViews();
  const size_t unlockedCount =
      std::count_if(views.begin(), views.end(), [](const AchievementView& view) { return view.state.unlocked; });
  return std::to_string(unlockedCount) + "/" + std::to_string(views.size());
}

std::string getSyncDayShortcutSubtitle() {
  const char* label = TimeUtils::getCurrentTimeZoneLabel();
  return (label != nullptr) ? label : "";
}

std::string getRecentBooksShortcutSubtitle() { return std::to_string(RECENT_BOOKS.getCount()); }

std::string getFavoritesShortcutSubtitle() { return std::to_string(FAVORITES.getCount()); }

std::string getFlashcardsShortcutSubtitle() {
  const int recentCount = static_cast<int>(FLASHCARDS.getRecentDecks().size());
  int statsCount = 0;
  for (const auto& record : FLASHCARDS.getKnownDecks()) {
    if (hasFlashcardStatsToShow(record)) {
      statsCount++;
    }
  }

  return std::to_string(recentCount) + " | " + std::to_string(statsCount);
}

std::string getSleepShortcutSubtitle() {
  const std::string selectedDirectory = SleepImageUtils::resolveConfiguredSleepDirectory();
  return SleepImageUtils::getDirectoryLabel(selectedDirectory);
}

std::string getFileTransferShortcutSubtitle() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isApMode = (wifiMode & WIFI_MODE_AP) != 0;
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) != 0 && WiFi.status() == WL_CONNECTED;

  if (isApMode) {
    return "AP";
  }
  if (isStaConnected) {
    const String ip = WiFi.localIP().toString();
    if (ip.length() > 0 && ip != "0.0.0.0") {
      return std::string(ip.c_str());
    }
    const String ssid = WiFi.SSID();
    return ssid.length() > 0 ? std::string(ssid.c_str()) : "WiFi";
  }
  return std::string(tr(STR_STATE_OFF));
}
}  // namespace

std::string ShortcutUiMetadata::getSubtitle(const ShortcutDefinition& definition) {
  switch (definition.id) {
    case ShortcutId::Stats:
    case ShortcutId::ReadingStats:
      return getStatsShortcutSubtitle();
    case ShortcutId::SyncDay:
      return getSyncDayShortcutSubtitle();
    case ShortcutId::Achievements:
      return getAchievementsShortcutSubtitle();
    case ShortcutId::RecentBooks:
      return getRecentBooksShortcutSubtitle();
    case ShortcutId::Favorites:
      return getFavoritesShortcutSubtitle();
    case ShortcutId::Flashcards:
      return getFlashcardsShortcutSubtitle();
    case ShortcutId::Sleep:
      return getSleepShortcutSubtitle();
    case ShortcutId::FileTransfer:
      return getFileTransferShortcutSubtitle();
    default:
      return (definition.descriptionId == StrId::STR_NONE_OPT) ? "" : std::string(I18N.get(definition.descriptionId));
  }
}

bool ShortcutUiMetadata::showAccessory(const ShortcutDefinition& definition) {
  return (definition.id == ShortcutId::Stats || definition.id == ShortcutId::ReadingStats) &&
         READING_STATS.getTodayReadingMs() >= getDailyReadingGoalMs();
}
