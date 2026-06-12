#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "SudokuGame.h"
#include "SudokuPuzzleBank.h"
#include "util/ButtonNavigator.h"

// Minimal Sudoku puzzle activity. Reads fixed-width puzzle banks from the SD
// card (see SudokuPuzzleBank), draws the 9x9 grid with plain digits and rules
// (see SudokuBoardRenderer), and tracks only the bare minimum of progress:
// which bank, which puzzle, and the player's current board so a half-finished
// puzzle can be resumed. There is no solver -- completion and the optional
// mistake-marking are derived purely from the Sudoku constraints. Entirely
// compiled out unless CPR_ENABLE_EXTRA_ACTIVITIES is enabled.
class SudokuActivity final : public Activity {
  enum class Mode {
    BankSelect,
    Playing,
  };

  // Power short-press swaps which role the direction buttons play, mirroring
  // Sokoban: by default they move the cursor around the grid; switched to
  // Entry they change the digit in the selected cell. Keeping the two on one
  // set of buttons avoids needing more keys than the device has.
  enum class ControlMode {
    Cursor,
    Entry,
  };

  static constexpr unsigned long kLongPressMs = 1000;

  ButtonNavigator buttonNavigator;
  std::string banksDir;

  Mode mode = Mode::BankSelect;
  std::vector<std::string> bankPaths;
  size_t bankSelectorIndex = 0;

  SudokuPuzzleBank bank;
  SudokuGame game;
  int puzzleIndex = 0;
  bool puzzleLoaded = false;
  bool lockLongPressConfirm = false;
  bool showConflicts = false;
  ControlMode controlMode = ControlMode::Cursor;

  std::string transientMessage;
  unsigned long transientUntilMs = 0;

  void scanBanks();
  bool openBank(const std::string& path);
  bool loadPuzzleAt(int index);
  void startRandomPuzzle();
  void toggleControlMode();
  void toggleConflicts();
  void exitToBankSelect();
  void handleSolved();
  void showTransientMessage(const std::string& message, unsigned long durationMs = 1500);
  void loadProgress();
  void saveProgress() const;
  void resumeSavedPuzzle(const std::string& bankPath);

  void renderBankSelect();
  void renderPlaying();

 public:
  explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string banksDir = "/sudoku")
      : Activity("Sudoku", renderer, mappedInput), banksDir(std::move(banksDir)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
