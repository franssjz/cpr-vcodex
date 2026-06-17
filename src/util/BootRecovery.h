#pragma once

#include <cstdint>

namespace BootRecovery {

enum class BootStage : uint8_t {
  None = 0,
  Settings,
  Language,
  KOReader,
  OPDS,
  UiTheme,
  DisplayAndFonts,
  State,
  ReadingStats,
  RecentBooks,
  Favorites,
  Flashcards,
  Achievements,
  RouteDecision,
  Completed,
};

void initialize();
void enterStage(BootStage stage);
void markBootCompleted();

BootStage getRecordedStage();
const char* getStageName(BootStage stage);

bool isRecoveryActive();
bool shouldForceHome();

bool shouldSkipSettings();
bool shouldSkipLanguage();
bool shouldSkipKOReader();
bool shouldSkipOPDS();
bool shouldSkipState();
bool shouldSkipReadingStats();
bool shouldSkipRecentBooks();
bool shouldSkipFavorites();
bool shouldSkipFlashcards();
bool shouldSkipAchievements();

}  // namespace BootRecovery
