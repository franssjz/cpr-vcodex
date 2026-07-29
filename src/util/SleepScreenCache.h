#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>

#include <SleepGreyStripSink.h>

class GfxRenderer;

// Streams grey LSB/MSB strips to the sleep cache on SD during decode so RAM stays bounded.
class SleepGreyscaleStreamWriter : public ISleepGreyStripSink {
 public:
  bool begin(const std::string& sourcePath, uint32_t sourceFileSize, uint32_t bufferSize, uint16_t panelWidthBytes,
             uint16_t panelHeight);
  bool isActive() const { return active_; }

  bool appendStrip(int yStart, int numRows, size_t stripBytes, const uint8_t* lsb, const uint8_t* msb) override;
  bool loadStrip(int yStart, int numRows, size_t stripBytes, uint8_t* lsb, uint8_t* msb) const override;
  void flushToDisplay(const GfxRenderer& renderer) const override;
  bool empty() const override { return !wroteStrip_; }

  bool commitBw(const uint8_t* bwBuffer, uint32_t bufferSize);
  void abort();

 private:
  static constexpr int STRIP_ROWS = 80;

  bool seekAndWrite(FsFile& file, size_t offset, const uint8_t* data, size_t length) const;
  bool seekAndRead(FsFile& file, size_t offset, uint8_t* data, size_t length) const;
  void closeWriteFiles();

  // Mutable so const loadStrip can seek/read while files remain open O_RDWR.
  mutable FsFile lsbFile_;
  mutable FsFile msbFile_;
  std::string sourcePath_;
  uint32_t sourceSize_ = 0;
  uint32_t bufferSize_ = 0;
  uint16_t panelWidthBytes_ = 0;
  uint16_t panelHeight_ = 0;
  std::string lsbPath_;
  std::string msbPath_;
  std::string bwPath_;
  bool active_ = false;
  bool wroteStrip_ = false;
};

class SleepScreenCache {
 public:
  static bool load(GfxRenderer& renderer, const std::string& sourcePath);
  // Loads BW into the framebuffer and verifies grey plane files exist. Does not touch
  // controller grey RAM — call applyGreyscalePlanes after displaySleepGrayscaleBase.
  static bool loadGreyscale(GfxRenderer& renderer, const std::string& sourcePath);
  // Streams cached LSB/MSB planes to the panel in strips (no full-plane heap alloc).
  static bool applyGreyscalePlanes(const GfxRenderer& renderer, const std::string& sourcePath);
  static void save(const GfxRenderer& renderer, const std::string& sourcePath);
  static int invalidateAll();

 private:
  static constexpr const char* CACHE_DIR = "/.crosspoint/sleep_cache";
  // Bump when cache file layout / grey encode semantics change so stale entries are ignored.
  static constexpr uint8_t CACHE_FORMAT_VERSION = 3;

  friend class SleepGreyscaleStreamWriter;

  static uint32_t hashKey(const std::string& sourcePath, uint32_t fileSize);
  static std::string getCachePath(const std::string& sourcePath, uint32_t fileSize, const char* suffix = ".raw");
  static bool readPlaneFile(const char* path, uint8_t* buffer, uint32_t bufferSize);
  static bool writePlaneFile(const char* path, const uint8_t* buffer, uint32_t bufferSize);
  static void removeEntry(const std::string& sourcePath, uint32_t sourceSize);
};
