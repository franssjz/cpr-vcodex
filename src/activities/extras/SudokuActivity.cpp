#include "SudokuActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdlib>

#include "SudokuBoardRenderer.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr char kModule[] = "SUDO";
constexpr char kProgressPath[] = "/.crosspoint/sudoku_progress.txt";

// Tiny "bank path\npuzzle index\ncurrent board" file -- a JsonDocument would
// be overkill for three fields, and this keeps the feature's footprint minimal
// (mirrors Sokoban's progress file).
bool readProgressFile(std::string& outBankPath, int& outIndex, std::string& outBoard) {
  if (!Storage.exists(kProgressPath)) return false;
  const String contents = Storage.readFile(kProgressPath);
  if (contents.isEmpty()) return false;

  const std::string text(contents.c_str());
  const size_t firstNewline = text.find('\n');
  if (firstNewline == std::string::npos) return false;
  const size_t secondNewline = text.find('\n', firstNewline + 1);
  if (secondNewline == std::string::npos) return false;

  outBankPath = text.substr(0, firstNewline);
  outIndex = atoi(text.substr(firstNewline + 1, secondNewline - firstNewline - 1).c_str());
  outBoard = text.substr(secondNewline + 1);
  // Strip any trailing newline left on the board line.
  const size_t boardEnd = outBoard.find_first_of("\r\n");
  if (boardEnd != std::string::npos) outBoard = outBoard.substr(0, boardEnd);
  return !outBankPath.empty();
}

void writeProgressFile(const std::string& bankPath, int index, const std::string& board) {
  Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite(kModule, kProgressPath, file)) return;
  const std::string contents = bankPath + "\n" + std::to_string(index) + "\n" + board;
  file.write(contents.c_str(), contents.size());
  file.flush();
  file.close();
}

}  // namespace

void SudokuActivity::scanBanks() {
  bankPaths = SudokuPuzzleBank::listBankPaths(banksDir);
  bankSelectorIndex = std::min(bankSelectorIndex, bankPaths.empty() ? size_t{0} : bankPaths.size() - 1);
}

void SudokuActivity::loadProgress() {
  std::string savedBankPath;
  int savedIndex = 0;
  std::string savedBoard;
  if (!readProgressFile(savedBankPath, savedIndex, savedBoard)) return;

  for (size_t i = 0; i < bankPaths.size(); i++) {
    if (bankPaths[i] == savedBankPath) {
      bankSelectorIndex = i;
      if (bank.open(savedBankPath) && loadPuzzleAt(savedIndex)) {
        game.applySavedBoard(savedBoard);
      }
      return;
    }
  }
}

// openBank() always starts a bank on a fresh random puzzle -- this restores the
// saved puzzle and board on top of that when the saved progress belongs to this
// bank, so re-opening a bank from the list resumes where the player left off.
void SudokuActivity::resumeSavedPuzzle(const std::string& bankPath) {
  std::string savedBankPath;
  int savedIndex = 0;
  std::string savedBoard;
  if (readProgressFile(savedBankPath, savedIndex, savedBoard) && savedBankPath == bankPath) {
    if (loadPuzzleAt(savedIndex)) {
      game.applySavedBoard(savedBoard);
    }
  }
}

void SudokuActivity::saveProgress() const {
  if (!bank.isOpen() || !puzzleLoaded) return;
  writeProgressFile(bank.path(), puzzleIndex, game.toBoardString());
}

bool SudokuActivity::openBank(const std::string& path) {
  if (!bank.open(path)) {
    showTransientMessage(tr(STR_SUDOKU_LOAD_ERROR));
    return false;
  }
  startRandomPuzzle();
  return puzzleLoaded;
}

bool SudokuActivity::loadPuzzleAt(int index) {
  std::string puzzle;
  if (!bank.loadPuzzle(index, puzzle) || !game.loadFromString(puzzle)) {
    puzzleLoaded = false;
    showTransientMessage(tr(STR_SUDOKU_LOAD_ERROR));
    return false;
  }
  puzzleIndex = index;
  puzzleLoaded = true;
  showConflicts = false;
  controlMode = ControlMode::Cursor;
  return true;
}

void SudokuActivity::startRandomPuzzle() {
  if (!bank.isOpen()) return;
  const int count = bank.puzzleCount();
  const int index = (count > 0) ? (rand() % count) : 0;
  loadPuzzleAt(index);
}

void SudokuActivity::toggleControlMode() {
  controlMode = (controlMode == ControlMode::Cursor) ? ControlMode::Entry : ControlMode::Cursor;
  showTransientMessage(controlMode == ControlMode::Entry ? tr(STR_SUDOKU_ENTRY_MODE) : tr(STR_SUDOKU_CURSOR_MODE));
}

void SudokuActivity::toggleConflicts() {
  showConflicts = !showConflicts;
  if (showConflicts) {
    showTransientMessage(game.hasConflicts() ? tr(STR_SUDOKU_MISTAKES_SHOWN) : tr(STR_SUDOKU_NO_MISTAKES));
  }
  requestUpdate();
}

void SudokuActivity::exitToBankSelect() {
  mode = Mode::BankSelect;
  controlMode = ControlMode::Cursor;
  showConflicts = false;
  bank.close();
  puzzleLoaded = false;
  scanBanks();
  requestUpdate();
}

void SudokuActivity::handleSolved() {
  showTransientMessage(tr(STR_SUDOKU_SOLVED), 2500);
  saveProgress();
}

void SudokuActivity::showTransientMessage(const std::string& message, unsigned long durationMs) {
  transientMessage = message;
  transientUntilMs = millis() + durationMs;
  requestUpdate();
}

void SudokuActivity::onEnter() {
  Activity::onEnter();
  srand(static_cast<unsigned>(millis()));
  controlMode = ControlMode::Cursor;
  showConflicts = false;
  scanBanks();
  loadProgress();
  if (bank.isOpen() && puzzleLoaded) {
    mode = Mode::Playing;
  } else {
    mode = Mode::BankSelect;
  }
  renderer.requestNextFullRefresh();
  requestUpdate(true);
}

void SudokuActivity::onExit() {
  saveProgress();
  bank.close();
  Activity::onExit();
}

void SudokuActivity::loop() {
  if (!transientMessage.empty() && millis() >= transientUntilMs) {
    transientMessage.clear();
    requestUpdate();
  }

  if (mode == Mode::BankSelect) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!bankPaths.empty()) {
        const std::string& path = bankPaths[bankSelectorIndex];
        if (openBank(path)) {
          resumeSavedPuzzle(path);
          mode = Mode::Playing;
          renderer.requestNextFullRefresh();
          requestUpdate(true);
        }
      }
      return;
    }

    const int listSize = static_cast<int>(bankPaths.size());
    buttonNavigator.onNextRelease([this, listSize] {
      bankSelectorIndex = ButtonNavigator::nextIndex(static_cast<int>(bankSelectorIndex), listSize);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, listSize] {
      bankSelectorIndex = ButtonNavigator::previousIndex(static_cast<int>(bankSelectorIndex), listSize);
      requestUpdate();
    });
    return;
  }

  // Mode::Playing
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveProgress();
    exitToBankSelect();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    toggleControlMode();
    return;
  }

  // Long-press Confirm toggles the mistake markers; the matching release is
  // swallowed so it doesn't also fire the short-press action.
  if (lockLongPressConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockLongPressConfirm = false;
    return;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= kLongPressMs) {
    lockLongPressConfirm = true;
    toggleConflicts();
    return;
  }

  if (!puzzleLoaded) return;

  if (game.isSolved()) {
    // Nothing left to edit -- a short Confirm starts a fresh random puzzle
    // from the same bank.
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startRandomPuzzle();
      saveProgress();
      renderer.requestNextFullRefresh();
      requestUpdate(true);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Short Confirm erases the current cell -- a quick way to undo a mistake.
    if (game.clearCurrent()) {
      showConflicts = false;
      requestUpdate();
    }
    return;
  }

  int dr = 0;
  int dc = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    dr = -1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    dr = 1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    dc = -1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    dc = 1;
  } else {
    return;
  }

  if (controlMode == ControlMode::Cursor) {
    game.moveCursor(dr, dc);
    requestUpdate();
    return;
  }

  // Entry mode: Up/Right step the digit forward, Down/Left step it back,
  // cycling through 1..9 and the empty state and writing live to the cell.
  const int delta = (dr < 0 || dc > 0) ? 1 : -1;
  if (game.cycleCurrentDigit(delta)) {
    showConflicts = false;
    requestUpdate();
    if (game.isSolved()) handleSolved();
  }
}

void SudokuActivity::render(RenderLock&&) {
  if (mode == Mode::BankSelect) {
    renderBankSelect();
  } else {
    renderPlaying();
  }
}

void SudokuActivity::renderBankSelect() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SUDOKU), tr(STR_SUDOKU_APP_DESC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (bankPaths.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_SUDOKU_NO_BANKS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(bankPaths.size()),
        static_cast<int>(bankSelectorIndex),
        [this](const int index) { return SudokuPuzzleBank::displayNameFromPath(bankPaths[index]); });
  }

  if (!transientMessage.empty() && millis() < transientUntilMs) {
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - metrics.buttonHintsHeight - 18, transientMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), bankPaths.empty() ? "" : tr(STR_OPEN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SudokuActivity::renderPlaying() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string bankName = SudokuPuzzleBank::displayNameFromPath(bank.path());
  const std::string subtitle = std::string(tr(STR_SUDOKU_REMAINING)) + ": " + std::to_string(game.remainingCells());
  HeaderDateUtils::drawHeaderWithDate(renderer, bankName.c_str(), subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int sideMargin = metrics.contentSidePadding;
  const int statsY = pageHeight - metrics.buttonHintsHeight - 18;

  if (puzzleLoaded) {
    SudokuBoardRenderer::draw(renderer, game, sideMargin, contentTop, pageWidth - 2 * sideMargin, contentHeight,
                              controlMode == ControlMode::Entry, showConflicts);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_SUDOKU_LOAD_ERROR));
  }

  if (!transientMessage.empty() && millis() < transientUntilMs) {
    renderer.drawCenteredText(SMALL_FONT_ID, statsY, transientMessage.c_str());
  }

  const char* confirmLabel =
      (puzzleLoaded && game.isSolved()) ? tr(STR_SUDOKU_NEW_PUZZLE) : tr(STR_SUDOKU_ERASE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
