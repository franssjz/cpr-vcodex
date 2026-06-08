#include "SokobanGame.h"

#include <algorithm>

namespace {

bool isBoxLike(SokobanGame::Cell cell) {
  return cell == SokobanGame::Cell::Box || cell == SokobanGame::Cell::BoxOnTarget;
}

SokobanGame::Cell cellFromXsbChar(char ch, bool& sawPlayer, int row, int col, int& playerRow, int& playerCol) {
  using Cell = SokobanGame::Cell;
  switch (ch) {
    case '#':
      return Cell::Wall;
    case '.':
      return Cell::Target;
    case '$':
      return Cell::Box;
    case '*':
      return Cell::BoxOnTarget;
    case '@':
      sawPlayer = true;
      playerRow = row;
      playerCol = col;
      return Cell::Player;
    case '+':
      sawPlayer = true;
      playerRow = row;
      playerCol = col;
      return Cell::PlayerOnTarget;
    case ' ':
    case '-':
    case '_':
    default:
      return Cell::Floor;
  }
}

}  // namespace

SokobanGame::Cell& SokobanGame::at(int row, int col) { return grid_[static_cast<size_t>(row) * cols_ + col]; }

SokobanGame::Cell SokobanGame::cellAt(int row, int col) const {
  if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
    return Cell::Wall;
  }
  return grid_[static_cast<size_t>(row) * cols_ + col];
}

bool SokobanGame::loadFromLines(const std::vector<std::string>& lines) {
  // Trim leading/trailing blank lines (XSB packs separate levels with them).
  size_t first = 0;
  size_t last = lines.size();
  while (first < last && lines[first].find_first_not_of(" \t\r") == std::string::npos) first++;
  while (last > first && lines[last - 1].find_first_not_of(" \t\r") == std::string::npos) last--;
  if (first >= last) return false;

  size_t maxCols = 0;
  for (size_t i = first; i < last; i++) {
    maxCols = std::max(maxCols, lines[i].size());
  }
  if (maxCols == 0) return false;

  rows_ = static_cast<int>(last - first);
  cols_ = static_cast<int>(maxCols);
  grid_.assign(static_cast<size_t>(rows_) * cols_, Cell::Floor);

  bool sawPlayer = false;
  for (int row = 0; row < rows_; row++) {
    const std::string& line = lines[first + static_cast<size_t>(row)];
    for (int col = 0; col < cols_; col++) {
      const char ch = (static_cast<size_t>(col) < line.size()) ? line[static_cast<size_t>(col)] : ' ';
      at(row, col) = cellFromXsbChar(ch, sawPlayer, row, col, playerRow_, playerCol_);
    }
  }
  if (!sawPlayer) {
    grid_.clear();
    rows_ = cols_ = 0;
    return false;
  }

  initialGrid_ = grid_;
  initialPlayerRow_ = playerRow_;
  initialPlayerCol_ = playerCol_;
  moveCount_ = 0;
  pushCount_ = 0;
  history_.clear();
  return true;
}

SokobanGame::Snapshot SokobanGame::makeSnapshot() const {
  Snapshot snap;
  snap.grid = grid_;
  snap.playerRow = playerRow_;
  snap.playerCol = playerCol_;
  snap.moveCount = moveCount_;
  snap.pushCount = pushCount_;
  return snap;
}

void SokobanGame::restoreSnapshot(const Snapshot& snap) {
  grid_ = snap.grid;
  playerRow_ = snap.playerRow;
  playerCol_ = snap.playerCol;
  moveCount_ = snap.moveCount;
  pushCount_ = snap.pushCount;
}

bool SokobanGame::move(int dr, int dc) {
  if (rows_ == 0 || cols_ == 0) return false;
  const int destRow = playerRow_ + dr;
  const int destCol = playerCol_ + dc;
  if (destRow < 0 || destRow >= rows_ || destCol < 0 || destCol >= cols_) return false;

  const Cell dest = cellAt(destRow, destCol);
  if (dest == Cell::Wall) return false;

  if (isBoxLike(dest)) {
    const int behindRow = destRow + dr;
    const int behindCol = destCol + dc;
    if (behindRow < 0 || behindRow >= rows_ || behindCol < 0 || behindCol >= cols_) return false;
    const Cell behind = cellAt(behindRow, behindCol);
    if (behind == Cell::Wall || isBoxLike(behind)) return false;
  }

  // All checks passed: record the snapshot, then mutate the grid.
  history_.push_back(makeSnapshot());
  if (history_.size() > static_cast<size_t>(kMaxUndo)) {
    history_.erase(history_.begin());
  }

  if (isBoxLike(dest)) {
    const int behindRow = destRow + dr;
    const int behindCol = destCol + dc;
    const Cell behind = cellAt(behindRow, behindCol);
    at(behindRow, behindCol) = (behind == Cell::Target) ? Cell::BoxOnTarget : Cell::Box;
    at(destRow, destCol) = (dest == Cell::BoxOnTarget) ? Cell::Target : Cell::Floor;
    pushCount_++;
  }

  const Cell vacated = cellAt(playerRow_, playerCol_);
  at(playerRow_, playerCol_) = (vacated == Cell::PlayerOnTarget) ? Cell::Target : Cell::Floor;

  const Cell arriving = cellAt(destRow, destCol);
  at(destRow, destCol) = (arriving == Cell::Target) ? Cell::PlayerOnTarget : Cell::Player;

  playerRow_ = destRow;
  playerCol_ = destCol;
  moveCount_++;
  return true;
}

bool SokobanGame::undo() {
  if (history_.empty()) return false;
  restoreSnapshot(history_.back());
  history_.pop_back();
  return true;
}

void SokobanGame::reset() {
  if (rows_ == 0 || cols_ == 0) return;
  grid_ = initialGrid_;
  playerRow_ = initialPlayerRow_;
  playerCol_ = initialPlayerCol_;
  moveCount_ = 0;
  pushCount_ = 0;
  history_.clear();
}

bool SokobanGame::isSolved() const {
  for (const Cell cell : grid_) {
    if (cell == Cell::Box) return false;
  }
  return rows_ > 0 && cols_ > 0;
}
