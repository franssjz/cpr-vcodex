#include "SudokuGame.h"

#include <algorithm>

int SudokuGame::parseCell(char c) {
  if (c >= '1' && c <= '9') return c - '0';
  if (c == '0' || c == '.' || c == '-' || c == ' ') return 0;
  return -1;
}

bool SudokuGame::loadFromString(const std::string& puzzle) {
  if (puzzle.size() != static_cast<size_t>(kCellCount)) return false;

  std::array<uint8_t, kCellCount> parsed{};
  for (int i = 0; i < kCellCount; i++) {
    const int v = parseCell(puzzle[static_cast<size_t>(i)]);
    if (v < 0) return false;
    parsed[static_cast<size_t>(i)] = static_cast<uint8_t>(v);
  }

  for (int i = 0; i < kCellCount; i++) {
    values_[static_cast<size_t>(i)] = parsed[static_cast<size_t>(i)];
    clues_[static_cast<size_t>(i)] = parsed[static_cast<size_t>(i)] != 0 ? 1 : 0;
  }
  cursorRow_ = 0;
  cursorCol_ = 0;
  return true;
}

void SudokuGame::reset() {
  for (int i = 0; i < kCellCount; i++) {
    if (!clues_[static_cast<size_t>(i)]) values_[static_cast<size_t>(i)] = 0;
  }
}

bool SudokuGame::applySavedBoard(const std::string& board) {
  if (board.size() != static_cast<size_t>(kCellCount)) return false;

  std::array<uint8_t, kCellCount> parsed{};
  for (int i = 0; i < kCellCount; i++) {
    const int v = parseCell(board[static_cast<size_t>(i)]);
    if (v < 0) return false;
    parsed[static_cast<size_t>(i)] = static_cast<uint8_t>(v);
  }

  // Clues always win over the saved board, so a stale or corrupt save can
  // never blank out or alter a given.
  for (int i = 0; i < kCellCount; i++) {
    if (!clues_[static_cast<size_t>(i)]) values_[static_cast<size_t>(i)] = parsed[static_cast<size_t>(i)];
  }
  return true;
}

std::string SudokuGame::toBoardString() const {
  std::string out(static_cast<size_t>(kCellCount), '.');
  for (int i = 0; i < kCellCount; i++) {
    const uint8_t v = values_[static_cast<size_t>(i)];
    if (v >= 1 && v <= 9) out[static_cast<size_t>(i)] = static_cast<char>('0' + v);
  }
  return out;
}

void SudokuGame::setValue(int row, int col, int value) {
  if (row < 0 || row >= kSize || col < 0 || col >= kSize) return;
  if (value < 0 || value > 9) return;
  const int i = index(row, col);
  if (clues_[static_cast<size_t>(i)]) return;
  values_[static_cast<size_t>(i)] = static_cast<uint8_t>(value);
}

void SudokuGame::moveCursor(int dr, int dc) {
  cursorRow_ = std::clamp(cursorRow_ + dr, 0, kSize - 1);
  cursorCol_ = std::clamp(cursorCol_ + dc, 0, kSize - 1);
}

bool SudokuGame::cycleCurrentDigit(int delta) {
  const int i = index(cursorRow_, cursorCol_);
  if (clues_[static_cast<size_t>(i)]) return false;
  // Cycle through the 10 states 0(empty),1,2,...,9 and wrap around.
  const int current = values_[static_cast<size_t>(i)];
  int next = ((current + delta) % 10 + 10) % 10;
  values_[static_cast<size_t>(i)] = static_cast<uint8_t>(next);
  return true;
}

bool SudokuGame::clearCurrent() {
  const int i = index(cursorRow_, cursorCol_);
  if (clues_[static_cast<size_t>(i)] || values_[static_cast<size_t>(i)] == 0) return false;
  values_[static_cast<size_t>(i)] = 0;
  return true;
}

bool SudokuGame::isComplete() const {
  for (int i = 0; i < kCellCount; i++) {
    if (values_[static_cast<size_t>(i)] == 0) return false;
  }
  return true;
}

bool SudokuGame::isCellConflicting(int row, int col) const {
  const int v = values_[index(row, col)];
  if (v == 0) return false;

  // Same row / same column.
  for (int k = 0; k < kSize; k++) {
    if (k != col && values_[index(row, k)] == v) return true;
    if (k != row && values_[index(k, col)] == v) return true;
  }

  // Same 3x3 box.
  const int boxRow = (row / kBox) * kBox;
  const int boxCol = (col / kBox) * kBox;
  for (int r = boxRow; r < boxRow + kBox; r++) {
    for (int c = boxCol; c < boxCol + kBox; c++) {
      if ((r != row || c != col) && values_[index(r, c)] == v) return true;
    }
  }
  return false;
}

bool SudokuGame::hasConflicts() const {
  for (int row = 0; row < kSize; row++) {
    for (int col = 0; col < kSize; col++) {
      if (isCellConflicting(row, col)) return true;
    }
  }
  return false;
}

int SudokuGame::remainingCells() const {
  int remaining = 0;
  for (int i = 0; i < kCellCount; i++) {
    if (values_[static_cast<size_t>(i)] == 0) remaining++;
  }
  return remaining;
}
