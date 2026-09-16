#include <JPEGDEC.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace {
std::array<int, 64> output;
bool outOfBounds = false;

int draw(JPEGDRAW* block) {
  const auto* pixels = reinterpret_cast<const unsigned char*>(block->pPixels);
  for (int y = 0; y < block->iHeight; ++y) {
    for (int x = 0; x < block->iWidthUsed; ++x) {
      const int px = block->x + x;
      const int py = block->y + y;
      if (px < 0 || px >= 8 || py < 0 || py >= 8) {
        outOfBounds = true;
        return 0;
      }
      output[py * 8 + px] = pixels[y * block->iWidth + x];
    }
  }
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {});
  if (data.empty()) return 2;
  output.fill(-1);
  auto jpeg = std::make_unique<JPEGDEC>();
  if (!jpeg->openRAM(data.data(), static_cast<int>(data.size()), draw)) return 3;
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  const int decoded = jpeg->decode(0, 0, JPEG_SCALE_EIGHTH);
  const int error = jpeg->getLastError();
  jpeg->close();
  if (decoded != 1 || outOfBounds) {
    std::fprintf(stderr, "Decode failed: result=%d error=%d bounds=%d\n", decoded, error, outOfBounds);
    return 1;
  }
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      const int expected = 24 + ((x + 3 * y) % 8) * 28;
      if (output[y * 8 + x] != expected) {
        std::fprintf(stderr, "Pixel (%d,%d): expected %d, got %d\n", x, y, expected, output[y * 8 + x]);
        return 1;
      }
    }
  }
  return 0;
}
