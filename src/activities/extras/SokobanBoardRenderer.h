#pragma once

#include "SokobanGame.h"

class GfxRenderer;

// Draws a SokobanGame board with plain geometric shapes (filled/outlined
// squares, a diamond for the player) instead of icon assets. This keeps the
// feature's flash footprint at zero when CPR_ENABLE_EXTRA_ACTIVITIES is off
// and the e-ink redraw cheap (a handful of fillRect/drawRect/fillPolygon
// calls per cell, no bitmap blits).
namespace SokobanBoardRenderer {

// Draws `game` centered inside the rectangle (x, y, width, height), choosing
// the largest integer cell size that fits the grid. Cells smaller than a few
// pixels are skipped (the level simply won't fit; callers should pick fonts
// and layouts that keep this from happening for the supported packs).
void draw(GfxRenderer& renderer, const SokobanGame& game, int x, int y, int width, int height);

}  // namespace SokobanBoardRenderer
