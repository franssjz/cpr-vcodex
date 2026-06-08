#include "SokobanBoardRenderer.h"

#include <algorithm>

#include "GfxRenderer.h"

namespace SokobanBoardRenderer {

namespace {

using Cell = SokobanGame::Cell;

void drawTargetMark(GfxRenderer& renderer, int x, int y, int size) {
  const int inset = std::max(2, size / 3);
  renderer.drawRect(x + inset, y + inset, size - 2 * inset, size - 2 * inset, true);
}

void drawBoxOutline(GfxRenderer& renderer, int x, int y, int size) {
  const int margin = std::max(1, size / 6);
  const int lineWidth = std::max(1, size / 10);
  renderer.drawRect(x + margin, y + margin, size - 2 * margin, size - 2 * margin, lineWidth, true);
}

void drawBoxOnTarget(GfxRenderer& renderer, int x, int y, int size) {
  const int margin = std::max(1, size / 6);
  renderer.fillRect(x + margin, y + margin, size - 2 * margin, size - 2 * margin, true);
}

void drawPlayer(GfxRenderer& renderer, int x, int y, int size) {
  const int margin = std::max(1, size / 6);
  const int cx = x + size / 2;
  const int cy = y + size / 2;
  const int xPoints[4] = {cx, x + size - margin, cx, x + margin};
  const int yPoints[4] = {y + margin, cy, y + size - margin, cy};
  renderer.fillPolygon(xPoints, yPoints, 4, true);
}

void drawCell(GfxRenderer& renderer, Cell cell, int x, int y, int size) {
  switch (cell) {
    case Cell::Wall:
      renderer.fillRect(x, y, size, size, true);
      break;
    case Cell::Floor:
      break;
    case Cell::Target:
      drawTargetMark(renderer, x, y, size);
      break;
    case Cell::Box:
      drawBoxOutline(renderer, x, y, size);
      break;
    case Cell::BoxOnTarget:
      drawBoxOnTarget(renderer, x, y, size);
      break;
    case Cell::Player:
      drawPlayer(renderer, x, y, size);
      break;
    case Cell::PlayerOnTarget:
      drawTargetMark(renderer, x, y, size);
      drawPlayer(renderer, x, y, size);
      break;
  }
}

}  // namespace

void draw(GfxRenderer& renderer, const SokobanGame& game, int x, int y, int width, int height) {
  const int rows = game.rows();
  const int cols = game.cols();
  if (rows <= 0 || cols <= 0) return;

  const int cellSize = std::min(width / cols, height / rows);
  if (cellSize < 4) return;

  const int boardWidth = cellSize * cols;
  const int boardHeight = cellSize * rows;
  const int originX = x + (width - boardWidth) / 2;
  const int originY = y + (height - boardHeight) / 2;

  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      drawCell(renderer, game.cellAt(row, col), originX + col * cellSize, originY + row * cellSize, cellSize);
    }
  }

  renderer.drawRect(originX, originY, boardWidth, boardHeight, true);
}

}  // namespace SokobanBoardRenderer
