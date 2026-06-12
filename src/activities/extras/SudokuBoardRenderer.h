#pragma once

#include "SudokuGame.h"

class GfxRenderer;

// Draws a SudokuGame board: a 9x9 grid with heavier rules between the 3x3
// boxes, clue digits in bold, the player's digits in a regular weight, a
// cursor box (drawn differently in entry mode so the player can tell which
// mode the buttons are in), and -- when requested -- an underline beneath
// cells that break the Sudoku constraints. Pure geometry and text, so the
// flash footprint stays at zero when CPR_ENABLE_EXTRA_ACTIVITIES is off.
namespace SudokuBoardRenderer {

// Draws `game` centered inside the rectangle (x, y, width, height), choosing
// the largest integer cell size that fits the 9x9 grid. When `showConflicts`
// is true, cells that duplicate a digit in their row/column/box are marked.
void draw(GfxRenderer& renderer, const SudokuGame& game, int x, int y, int width, int height, bool entryMode,
          bool showConflicts);

}  // namespace SudokuBoardRenderer
