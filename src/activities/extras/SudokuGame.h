#pragma once

#include <array>
#include <cstdint>
#include <string>

// Self-contained Sudoku rules engine: the 9x9 grid, fixed "clue" cells from
// the puzzle, the player's pencilled-in digits, a cursor, conflict detection
// and win detection. There is deliberately no solver -- completion and the
// "mark mistakes" helper are both implemented purely from the standard Sudoku
// constraints (no duplicate digit in any row, column or 3x3 box). Since the
// supported puzzle banks have unique solutions, "all cells filled with zero
// conflicts" is equivalent to "solved", so a solver is not needed.
//
// No rendering or storage dependencies, so it stays easy to test and to drop
// along with the rest of CPR_ENABLE_EXTRA_ACTIVITIES if this feature is ever
// removed.
class SudokuGame {
 public:
  static constexpr int kSize = 9;
  static constexpr int kBox = 3;
  static constexpr int kCellCount = kSize * kSize;

  // Parses an 81-character puzzle string. '1'-'9' are clues (fixed givens);
  // '0', '.', '-' or ' ' are empty cells. Any other length or character makes
  // this return false. Resets the cursor to the top-left.
  bool loadFromString(const std::string& puzzle);

  // Clears all of the player's entries, leaving only the original clues.
  void reset();

  // Overlays a previously saved 81-character board (same encoding as
  // loadFromString) on top of the current clues. Clue cells are never
  // overwritten, so a corrupt save can't turn a given into a blank. Must be
  // called after loadFromString(). Returns false if the string is malformed.
  bool applySavedBoard(const std::string& board);

  // The player's current board as an 81-character string ('.' for empty),
  // suitable for saving and feeding back to applySavedBoard().
  std::string toBoardString() const;

  bool isClue(int row, int col) const { return clues_[index(row, col)] != 0; }
  int valueAt(int row, int col) const { return values_[index(row, col)]; }

  // Sets the player's digit in (row, col). Clue cells are left untouched.
  // value must be in 0..9 (0 clears the cell).
  void setValue(int row, int col, int value);

  // Cursor movement (clamped to the board; does not wrap).
  int cursorRow() const { return cursorRow_; }
  int cursorCol() const { return cursorCol_; }
  void moveCursor(int dr, int dc);

  // Cycles the current cell's digit by delta (e.g. +1 / -1), wrapping through
  // 1..9 and the empty state. No-op on clue cells. Returns true if it changed.
  bool cycleCurrentDigit(int delta);

  // Clears the current cell (no-op on clue cells). Returns true if it changed.
  bool clearCurrent();

  bool isComplete() const;  // every cell filled (says nothing about validity)

  // True if the digit in (row, col) duplicates another digit in the same row,
  // column or 3x3 box. Empty cells never conflict.
  bool isCellConflicting(int row, int col) const;
  bool hasConflicts() const;

  // Solved == fully filled with no conflicts. For unique-solution puzzles this
  // is exactly the solution, so no solver is required.
  bool isSolved() const { return isComplete() && !hasConflicts(); }

  // Number of cells the player has still to fill in.
  int remainingCells() const;

 private:
  static int index(int row, int col) { return row * kSize + col; }
  static int parseCell(char c);  // -1 invalid, 0 empty, 1..9 digit

  std::array<uint8_t, kCellCount> values_{};  // 0 = empty, 1..9 = digit
  std::array<uint8_t, kCellCount> clues_{};   // non-zero where the cell is a given
  int cursorRow_ = 0;
  int cursorCol_ = 0;
};
