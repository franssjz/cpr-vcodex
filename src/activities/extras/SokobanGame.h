#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Self-contained Sokoban rules engine: grid state, moves, a short undo
// history, and win detection. No rendering or storage dependencies, so it
// stays easy to unit-test and to remove along with the rest of
// CPR_ENABLE_EXTRA_ACTIVITIES if this feature is ever dropped.
class SokobanGame {
 public:
  enum class Cell : uint8_t {
    Wall,
    Floor,
    Target,
    Box,
    BoxOnTarget,
    Player,
    PlayerOnTarget,
  };

  // Keep undo cheap: a handful of full-grid snapshots is a few KB at most for
  // the largest known XSB packs (~18x30 cells). If the player goes further
  // wrong than this, restarting the level is the intended recovery path.
  static constexpr int kMaxUndo = 3;

  // Parses an XSB-style level (lines of '#', ' ', '.', '@', '+', '$', '*',
  // blank/'-'/'_' for floor). Returns false if the level has no player or no
  // cells at all.
  bool loadFromLines(const std::vector<std::string>& lines);

  // Attempts to move the player by (dr, dc) in {-1, 0, 1}. Pushes a box if
  // one is in the way and the cell behind it is free. Returns true if the
  // player actually moved (and records an undo snapshot).
  bool move(int dr, int dc);

  // Restores the previous snapshot, if any. Returns false when history is
  // empty (the move counter and pushes counter are restored too).
  bool undo();

  // Restores the level to the state right after loadFromLines().
  void reset();

  bool isSolved() const;

  int rows() const { return rows_; }
  int cols() const { return cols_; }
  int moveCount() const { return moveCount_; }
  int pushCount() const { return pushCount_; }
  Cell cellAt(int row, int col) const;

 private:
  struct Snapshot {
    std::vector<Cell> grid;
    int playerRow = 0;
    int playerCol = 0;
    int moveCount = 0;
    int pushCount = 0;
  };

  Cell& at(int row, int col);
  Snapshot makeSnapshot() const;
  void restoreSnapshot(const Snapshot& snap);

  std::vector<Cell> grid_;
  std::vector<Cell> initialGrid_;
  int rows_ = 0;
  int cols_ = 0;
  int playerRow_ = 0;
  int playerCol_ = 0;
  int initialPlayerRow_ = 0;
  int initialPlayerCol_ = 0;
  int moveCount_ = 0;
  int pushCount_ = 0;
  std::vector<Snapshot> history_;
};
