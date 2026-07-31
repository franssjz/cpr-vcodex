#pragma once

#include <cstddef>
#include <cstdint>

class GfxRenderer;

// Receives grey LSB/MSB strips during a single-pass sleep BMP decode.
// Strips may be written in any physical-Y order (Portrait maps logical X -> phyY).
class ISleepGreyStripSink {
 public:
  virtual ~ISleepGreyStripSink() = default;
  virtual bool appendStrip(int yStart, int numRows, size_t stripBytes, const uint8_t* lsb, const uint8_t* msb) = 0;
  // Load a previously written strip back into the scratch buffers. Returns false if missing
  // (caller should treat the strip as zeros).
  virtual bool loadStrip(int yStart, int numRows, size_t stripBytes, uint8_t* lsb, uint8_t* msb) const = 0;
  virtual void flushToDisplay(const GfxRenderer& renderer) const = 0;
  virtual bool empty() const = 0;
};
