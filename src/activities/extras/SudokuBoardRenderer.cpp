#include "SudokuBoardRenderer.h"

#include <algorithm>
#include <string>

#include "GfxRenderer.h"
#include "fontIds.h"

namespace SudokuBoardRenderer {

namespace {

constexpr int kSize = SudokuGame::kSize;
constexpr int kBox = SudokuGame::kBox;

void drawDigit(GfxRenderer& renderer, int value, int x, int y, int size, bool bold) {
  if (value <= 0) return;
  const char text[2] = {static_cast<char>('0' + value), '\0'};
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int fontId = (size >= 26) ? UI_12_FONT_ID : (size >= 18 ? UI_10_FONT_ID : SMALL_FONT_ID);
  const int textWidth = renderer.getTextWidth(fontId, text, style);
  const int textHeight = renderer.getTextHeight(fontId);
  renderer.drawText(fontId, x + (size - textWidth) / 2, y + (size - textHeight) / 2, text, true, style);
}

}  // namespace

void draw(GfxRenderer& renderer, const SudokuGame& game, int x, int y, int width, int height, bool entryMode,
          bool showConflicts) {
  const int cellSize = std::min(width / kSize, height / kSize);
  if (cellSize < 8) return;

  const int boardSize = cellSize * kSize;
  const int originX = x + (width - boardSize) / 2;
  const int originY = y + (height - boardSize) / 2;

  const int curX = originX + game.cursorCol() * cellSize;
  const int curY = originY + game.cursorRow() * cellSize;

  // Digits.
  for (int row = 0; row < kSize; row++) {
    for (int col = 0; col < kSize; col++) {
      const int value = game.valueAt(row, col);
      if (value <= 0) continue;
      const int cx = originX + col * cellSize;
      const int cy = originY + row * cellSize;
      drawDigit(renderer, value, cx, cy, cellSize, game.isClue(row, col));
      if (showConflicts && game.isCellConflicting(row, col)) {
        // Underline rule-breaking cells -- reads as an "error" without
        // obscuring the digit itself.
        const int margin = std::max(2, cellSize / 6);
        const int underY = cy + cellSize - margin;
        renderer.drawLine(cx + margin, underY, cx + cellSize - margin, underY, std::max(1, cellSize / 12), true);
      }
    }
  }

  // Thin grid lines, then heavier lines on the 3x3 box boundaries. Drawn as
  // filled rectangles centered on each boundary rather than via the thick
  // drawLine() overload, which only thickens horizontal lines (it offsets in
  // +Y), so vertical box rules would otherwise stay 1px wide.
  for (int i = 0; i <= kSize; i++) {
    const int lineWidth = (i % kBox == 0) ? std::max(2, cellSize / 10) : 1;
    const int half = lineWidth / 2;
    const int gx = originX + i * cellSize;
    const int gy = originY + i * cellSize;
    renderer.fillRect(gx - half, originY, lineWidth, boardSize + 1, true);
    renderer.fillRect(originX, gy - half, boardSize + 1, lineWidth, true);
  }

  // Cursor outline: a heavier double border in entry mode so the two button
  // modes are visually distinct.
  const int cursorWidth = entryMode ? std::max(3, cellSize / 6) : std::max(2, cellSize / 10);
  renderer.drawRect(curX, curY, cellSize, cellSize, cursorWidth, true);
}

}  // namespace SudokuBoardRenderer
