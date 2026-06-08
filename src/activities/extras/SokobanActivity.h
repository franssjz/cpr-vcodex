#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "SokobanGame.h"
#include "SokobanLevelPack.h"
#include "util/ButtonNavigator.h"

// Minimal Sokoban puzzle activity. Reads XSB level packs from the SD card
// (see SokobanLevelPack), draws the board with plain geometric shapes (see
// SokobanBoardRenderer), and tracks only the bare minimum of progress: which
// pack and which level the player last had open. Entirely compiled out
// unless CPR_ENABLE_EXTRA_ACTIVITIES is enabled.
class SokobanActivity final : public Activity {
  enum class Mode {
    PackSelect,
    Playing,
  };

  // The device's side buttons double as both Up/Down (movement) and
  // PageBack/PageForward (level navigation) -- they're the same physical
  // buttons. Power short-press swaps which role they perform, so the player
  // can move around the puzzle by default and switch to flipping between
  // levels without the two meanings colliding.
  enum class ControlMode {
    Move,
    LevelNav,
  };

  static constexpr unsigned long kLongPressMs = 1000;
  // After this many consecutive Undo presses (with no move in between), the
  // player is likely stuck rather than fixing a small mistake -- offer a
  // restart instead of letting them grind out one undo at a time.
  static constexpr int kUndoRestartThreshold = 5;

  ButtonNavigator buttonNavigator;
  std::string levelsDir;

  Mode mode = Mode::PackSelect;
  std::vector<std::string> packPaths;
  size_t packSelectorIndex = 0;

  SokobanLevelPack pack;
  SokobanGame game;
  int levelIndex = 0;
  bool levelLoaded = false;
  bool lockLongPressBack = false;
  bool lockLongPressConfirm = false;
  int consecutiveUndoPresses = 0;
  ControlMode controlMode = ControlMode::Move;

  std::string transientMessage;
  unsigned long transientUntilMs = 0;

  void scanPacks();
  bool openPack(const std::string& path);
  bool loadLevel(int index);
  void goToLevel(int delta);
  void restartLevel();
  void confirmRestart();
  void toggleControlMode();
  void exitToPackSelect();
  void showTransientMessage(const std::string& message, unsigned long durationMs = 1500);
  void loadProgress();
  void saveProgress() const;
  void resumeSavedLevel(const std::string& packPath);

  void renderPackSelect();
  void renderPlaying();

 public:
  explicit SokobanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           std::string levelsDir = "/sokoban")
      : Activity("Sokoban", renderer, mappedInput), levelsDir(std::move(levelsDir)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
