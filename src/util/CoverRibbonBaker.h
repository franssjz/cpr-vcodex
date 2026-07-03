#pragma once

#include <cstdint>
#include <string>

// Shared 1-bit BMP ribbon baking for both Library grid covers and Lyra Carousel
// thumbnails. Callers provide an in-memory pixel buffer; the baker draws
// corner ribbons and bottom bands directly into it.

namespace CoverRibbonBaker {

// Row stride for a 1-bit BMP of width w (rows padded to 4-byte boundary).
int bmpRowBytes(int w);

// Apply all ribbon indicators (completed, favorite, opened) to a 1-bit BMP
// pixel buffer. Returns true if any pixel was changed.
bool applyRibbons(uint8_t* pixels, int w, int h, const std::string& bookPath);

// Open an existing BMP on disk at |coverPath|, decode its pixel data,
// apply ribbons in-place, and write the modified pixels back.
// Does nothing if the file can't be opened or no ribbons are needed.
void bakeIntoFile(const std::string& coverPath, const std::string& bookPath);

}  // namespace CoverRibbonBaker