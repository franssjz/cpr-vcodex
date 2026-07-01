#include "CoverRibbonBaker.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "FavoritesStore.h"
#include "ReadingStatsStore.h"

namespace {
constexpr int kRibbonBorder = 2;

void setPixel(uint8_t* buf, int rowBytes, int x, int y, bool black) {
  int byteIdx = y * rowBytes + (x >> 3);
  int bitIdx = 7 - (x & 7);
  if (black) {
    buf[byteIdx] |= (1U << bitIdx);
  } else {
    buf[byteIdx] &= ~(1U << bitIdx);
  }
}

void drawHLine(uint8_t* buf, int rowBytes, int x1, int x2, int y) {
  if (x1 > x2) std::swap(x1, x2);
  for (int x = x1; x <= x2; ++x) setPixel(buf, rowBytes, x, y, true);
}

void drawTopRightTriangle(uint8_t* buf, int rowBytes, int w, int h, int leg) {
  for (int dy = 0; dy < leg; ++dy) {
    drawHLine(buf, rowBytes, w - (leg - dy), w - 1, dy);
  }
}

void drawTopLeftTriangle(uint8_t* buf, int rowBytes, int w, int h, int leg) {
  for (int dy = 0; dy < leg; ++dy) {
    drawHLine(buf, rowBytes, 0, leg - 1 - dy, dy);
  }
}

void drawReadRibbon(uint8_t* buf, int rowBytes, int w, int h) {
  const int leg = std::max(std::min(w * 9 / 20, 36), 14);
  drawTopRightTriangle(buf, rowBytes, w, h, leg);
  // White inner border
  for (int dy = kRibbonBorder; dy < leg - kRibbonBorder; ++dy) {
    int left = w - (leg - dy) + kRibbonBorder;
    int right = w - 1 - kRibbonBorder;
    for (int x = left; x <= right; ++x) setPixel(buf, rowBytes, x, dy, false);
  }
  // White checkmark
  const int cx = w - leg / 2 - 1;
  const int cy = leg / 2 - 3;
  for (int i = 0; i < 4; ++i) { setPixel(buf, rowBytes, cx - 7 + i, cy + i, false); }
  for (int i = 0; i < 5; ++i) { setPixel(buf, rowBytes, cx - 3 + i, cy + 4 - i, false); }
}

void drawFavoriteRibbon(uint8_t* buf, int rowBytes, int w, int h) {
  const int leg = std::max(std::min(w * 9 / 20, 36), 14);
  drawTopLeftTriangle(buf, rowBytes, w, h, leg);
  // White inner border
  for (int dy = kRibbonBorder; dy < leg - kRibbonBorder; ++dy) {
    int left = kRibbonBorder;
    int right = leg - 1 - dy - kRibbonBorder;
    for (int x = left; x <= right; ++x) setPixel(buf, rowBytes, x, dy, false);
  }
  // White heart (approx 11x10 pixel heart centered in triangle)
  const int hx = leg / 3;
  const int hy = leg / 4 + 2;
  // Row 0:  x x x _ _ _ _ x x x
  for (int dx : {1, 2, 8, 9}) setPixel(buf, rowBytes, hx + dx, hy, false);
  // Row 2-4: full bar
  for (int dy = 2; dy <= 4; ++dy)
    for (int dx = 0; dx < 11; ++dx)
      setPixel(buf, rowBytes, hx + dx, hy + dy, false);
  // Row 6:  _ x x x x x x x x x _
  for (int dx = 1; dx < 10; ++dx)
    setPixel(buf, rowBytes, hx + dx, hy + 6, false);
  // Row 8:  _ _ _ x x x x x _ _ _
  for (int dx = 3; dx < 8; ++dx)
    setPixel(buf, rowBytes, hx + dx, hy + 8, false);
  // Row 9:  _ _ _ _ x x x _ _ _ _
  for (int dx = 4; dx < 7; ++dx)
    setPixel(buf, rowBytes, hx + dx, hy + 9, false);
}

void drawOpenedBand(uint8_t* buf, int rowBytes, int w, int h) {
  const int bandH = std::max(4, h / 15);
  const int bandY = h - bandH;
  for (int dy = 0; dy < bandH; ++dy)
    for (int x = 0; x < w; ++x) setPixel(buf, rowBytes, x, bandY + dy, true);
  // White dots in the center
  const int dotR = std::max(1, bandH / 3);
  const int dotCY = bandY + bandH / 2;
  const int spacing = std::max(6, w / 12);
  for (int dot = -1; dot <= 1; dot += 2) {
    int dotCX = w / 2 + dot * spacing;
    for (int dy = -dotR; dy <= dotR; ++dy) {
      for (int dx = -dotR; dx <= dotR; ++dx) {
        if (dx * dx + dy * dy <= dotR * dotR + dotR) {
          int px = dotCX + dx;
          int py = dotCY + dy;
          if (px >= 0 && px < w && py >= 0 && py < h)
            setPixel(buf, rowBytes, px, py, false);
        }
      }
    }
  }
}

// --- BMP file I/O helpers ---

// BMP stores rows bottom-up. After reading, flip the row order so our
// drawing functions can use Y=0 as the top of the image. Flip back before
// writing so the file remains a valid bottom-up BMP.

static void bmpFlipRows(uint8_t* buf, int rowBytes, int h) {
  std::vector<uint8_t> tmp(rowBytes);
  for (int top = 0, bot = h - 1; top < bot; ++top, --bot) {
    memcpy(tmp.data(), buf + top * rowBytes, rowBytes);
    memcpy(buf + top * rowBytes, buf + bot * rowBytes, rowBytes);
    memcpy(buf + bot * rowBytes, tmp.data(), rowBytes);
  }
}

bool bmpParseDimensions(FsFile& f, int& outW, int& outH) {
  if (!f.seek(0)) return false;
  if (!f.seek(18)) return false;
  uint8_t dimBytes[8];
  if (f.read(dimBytes, 8) < 8) return false;
  auto le32 = [](const uint8_t* b) -> int32_t {
    return static_cast<int32_t>(b[0]) | (static_cast<int32_t>(b[1]) << 8) |
           (static_cast<int32_t>(b[2]) << 16) | (static_cast<int32_t>(b[3]) << 24);
  };
  outW = static_cast<int>(le32(dimBytes));
  outH = static_cast<int>(le32(dimBytes + 4));
  return outW > 0 && outH > 0;
}

// bfOffBits must be obtained from the read pass — this function is called
// on a write-only handle, so it cannot read the BMP header.
bool bmpRewriteWithPixels(FsFile& file, const uint8_t* pixelData, int w, int h, uint32_t bfOffBits) {
  if (!file.seek(static_cast<int>(bfOffBits))) return false;
  int rowBytes = CoverRibbonBaker::bmpRowBytes(w);
  size_t total = static_cast<size_t>(rowBytes) * h;
  return file.write(pixelData, total) == total;
}
}  // namespace

namespace CoverRibbonBaker {

int bmpRowBytes(int w) { return ((w + 31) / 32) * 4; }

bool applyRibbons(uint8_t* pixels, int w, int h, const std::string& bookPath) {
  int rowBytes = bmpRowBytes(w);
  bool changed = false;

  const auto* stats = READING_STATS.findBook(bookPath);
  if (stats && stats->completed) {
    drawReadRibbon(pixels, rowBytes, w, h);
    changed = true;
  }
  if (FAVORITES.isFavorite(bookPath)) {
    drawFavoriteRibbon(pixels, rowBytes, w, h);
    changed = true;
  }
  if (stats && stats->totalReadingMs > 0 && !stats->completed) {
    drawOpenedBand(pixels, rowBytes, w, h);
    changed = true;
  }
  return changed;
}

void bakeIntoFile(const std::string& coverPath, const std::string& bookPath) {
  FsFile f;
  if (!Storage.openFileForRead("LIB", coverPath, f)) return;
  int w = 0, h = 0;
  if (!bmpParseDimensions(f, w, h)) { f.close(); return; }
  // Read bfOffBits while we have the read handle open
  uint32_t bfOffBits = 0;
  if (f.seek(10)) {
    uint8_t ob[4];
    if (f.read(ob, 4) == 4)
      bfOffBits = ob[0] | (static_cast<uint32_t>(ob[1]) << 8) |
                  (static_cast<uint32_t>(ob[2]) << 16) | (static_cast<uint32_t>(ob[3]) << 24);
  }
  if (bfOffBits == 0) { f.close(); return; }
  int rowBytes = bmpRowBytes(w);
  size_t pixelBytes = static_cast<size_t>(rowBytes) * h;
  if (pixelBytes > 256000) { f.close(); return; }
  std::vector<uint8_t> pixels(pixelBytes);
  // Seek to pixel data
  if (!f.seek(static_cast<int>(bfOffBits))) { f.close(); return; }
  if (f.read(pixels.data(), pixelBytes) < pixelBytes) { f.close(); return; }
  f.close();

  // BMP is bottom-up: flip rows so Y=0 means top of image for our drawing code
  bmpFlipRows(pixels.data(), rowBytes, h);

  if (!applyRibbons(pixels.data(), w, h, bookPath)) return;

  // Flip back to bottom-up before writing to disk
  bmpFlipRows(pixels.data(), rowBytes, h);

  FsFile fw;
  if (!Storage.openFileForWrite("LIB", coverPath, fw)) return;
  bmpRewriteWithPixels(fw, pixels.data(), w, h, bfOffBits);
  fw.close();
}

}  // namespace CoverRibbonBaker