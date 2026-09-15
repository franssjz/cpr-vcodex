#pragma once

#include <cstddef>
#include <cstdint>

// On-disk format for the .pxc cache. The payload contains already-quantized
// 2-bit pixels, so changing the tone curve or dithering without invalidating
// the file would keep showing the calibration from an older firmware.
//
// Layout (little-endian, 8 bytes):
//   uint16 magic
//   uint8  version
//   uint8  variant
//   uint16 width
//   uint16 height
constexpr uint16_t PXC_MAGIC = 0x5850;  // 'P','X'; not a plausible panel width

// Bump whenever cache quantization changes (tone curve, thresholds, dither
// matrix, or adaptive calibration). Version 1 corresponds to the proportional
// four-level Bayer calibration introduced in CPR-vCodex 1.6.0.32.
constexpr uint8_t PXC_VERSION = 1;
constexpr size_t PXC_HEADER_BYTES = 8;

// Kept in the format from day one so a future factory/absolute grayscale path
// cannot accidentally reuse pixels tuned for the differential waveform.
enum class PixelCacheVariant : uint8_t {
  Differential = 0,
  FactoryLut = 1,
};

constexpr size_t pxcExpectedSize(const uint16_t width, const uint16_t height) {
  return PXC_HEADER_BYTES + static_cast<size_t>((width + 3) / 4) * height;
}
