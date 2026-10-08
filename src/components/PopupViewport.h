#pragma once

#include <algorithm>

struct PopupRowBounds {
  int top = 0;
  int height = 0;
  int bottom() const { return top + height; }
};

// Pixel scrolling also handles wrapped rows taller than one screenful.
struct PopupViewport {
  int offset = 0;
  int height = 0;
  int total = 0;
  int lineHeight = 1;

  int maximum() const { return std::max(0, total - height); }
  int pageStep() const { return std::max(1, height - lineHeight); }
  bool visible(const PopupRowBounds& row) const { return row.bottom() > offset && row.top < offset + height; }

  void configure(int contentHeight, int viewportHeight, int textLineHeight) {
    total = contentHeight;
    height = std::max(1, viewportHeight);
    lineHeight = std::max(1, textLineHeight);
    offset = std::clamp(offset, 0, maximum());
  }

  void scroll(int delta) { offset = std::clamp(offset + delta, 0, maximum()); }

  void reveal(const PopupRowBounds& row) {
    if (row.top < offset || row.height > height) {
      offset = row.top;
    } else if (row.bottom() > offset + height) {
      offset = row.bottom() - height;
    }
    offset = std::clamp(offset, 0, maximum());
  }

  int navigate(int direction, int selected, const PopupRowBounds* rows, int count) {
    if (count <= 0) return 0;
    const auto& row = rows[selected];
    // Read the rest of the current row/body before moving to another option.
    if (direction > 0 && row.bottom() > offset + height) {
      scroll(std::min(pageStep(), row.bottom() - offset - height));
      return selected;
    }
    if (direction < 0 && (row.top < offset || (selected == 0 && offset > 0))) {
      scroll(-std::min(pageStep(), selected == 0 ? offset : offset - row.top));
      return selected;
    }
    selected = (selected + direction + count) % count;
    if (selected == 0 && direction > 0) offset = 0;
    reveal(rows[selected]);
    return selected;
  }
};
