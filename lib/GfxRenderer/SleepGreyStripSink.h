#pragma once

#include <cstddef>
#include <cstdint>

class GfxRenderer;

// Receives grey LSB/MSB strips during a single-pass sleep BMP decode.
class ISleepGreyStripSink {
 public:
  virtual ~ISleepGreyStripSink() = default;
  virtual bool appendStrip(int yStart, int numRows, size_t stripBytes, const uint8_t* lsb, const uint8_t* msb) = 0;
  virtual void flushToDisplay(const GfxRenderer& renderer) const = 0;
  virtual bool empty() const = 0;
};
