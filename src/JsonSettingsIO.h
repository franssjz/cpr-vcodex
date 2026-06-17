#pragma once

class CrossPointSettings;
class CrossPointState;
class WifiCredentialStore;
class KOReaderCredentialStore;
class RecentBooksStore;
class FavoritesStore;
class ReadingStatsStore;
class AchievementsStore;
class OpdsServerStore;

namespace JsonSettingsIO {

// CrossPointSettings
bool saveSettings(const CrossPointSettings& s, const char* path);
bool loadSettings(CrossPointSettings& s, const char* json, bool* needsResave = nullptr);

// CrossPointState
bool saveState(const CrossPointState& s, const char* path);
bool loadState(CrossPointState& s, const char* json);

// WifiCredentialStore
bool saveWifi(const WifiCredentialStore& store, const char* path);
bool loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave = nullptr);

// KOReaderCredentialStore
bool saveKOReader(const KOReaderCredentialStore& store, const char* path);
bool loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave = nullptr);

// RecentBooksStore
bool saveRecentBooks(const RecentBooksStore& store, const char* path);
bool loadRecentBooks(RecentBooksStore& store, const char* json);

// FavoritesStore
bool saveFavorites(const FavoritesStore& store, const char* path);
bool loadFavorites(FavoritesStore& store, const char* json);

// ReadingStatsStore
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);
bool saveAchievements(const AchievementsStore& store, const char* path);
bool loadAchievements(AchievementsStore& store, const char* json);
bool loadAchievementsFromFile(AchievementsStore& store, const char* path);

// OpdsServerStore
bool saveOpds(const OpdsServerStore& store, const char* path);
bool loadOpds(OpdsServerStore& store, const char* json, bool* needsResave = nullptr);

}  // namespace JsonSettingsIO
