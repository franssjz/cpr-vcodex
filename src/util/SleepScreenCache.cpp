#include "SleepScreenCache.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

#include "CrossPointSettings.h"

namespace {
uint32_t getSourceFileSize(const std::string& sourcePath) {
  FsFile file;
  if (!Storage.openFileForRead("SLC", sourcePath, file)) {
    return 0;
  }
  const uint32_t size = file.fileSize();
  file.close();
  return size;
}
}  // namespace

uint32_t SleepScreenCache::hashKey(const std::string& sourcePath, const uint32_t fileSize) {
  uint32_t hash = 2166136261u;
  for (char c : sourcePath) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  for (int i = 0; i < 4; i++) {
    hash ^= static_cast<uint8_t>((fileSize >> (i * 8)) & 0xFF);
    hash *= 16777619u;
  }
  hash ^= static_cast<uint8_t>(SETTINGS.sleepScreenCoverFilter);
  hash *= 16777619u;
  hash ^= static_cast<uint8_t>(SETTINGS.sleepScreenCoverMode);
  hash *= 16777619u;
  hash ^= CACHE_FORMAT_VERSION;
  hash *= 16777619u;
  return hash;
}

std::string SleepScreenCache::getCachePath(const std::string& sourcePath, const uint32_t fileSize, const char* suffix) {
  char filename[64];
  snprintf(filename, sizeof(filename), "%s/%08x%s", CACHE_DIR, hashKey(sourcePath, fileSize),
           suffix != nullptr ? suffix : ".raw");
  return std::string(filename);
}

void SleepScreenCache::removeEntry(const std::string& sourcePath, const uint32_t sourceSize) {
  Storage.remove(getCachePath(sourcePath, sourceSize, ".raw").c_str());
  Storage.remove(getCachePath(sourcePath, sourceSize, ".lsb.raw").c_str());
  Storage.remove(getCachePath(sourcePath, sourceSize, ".msb.raw").c_str());
}

bool SleepScreenCache::readPlaneFile(const char* path, uint8_t* buffer, const uint32_t bufferSize) {
  FsFile file;
  if (!Storage.openFileForRead("SLC", path, file)) {
    return false;
  }

  if (file.fileSize() != bufferSize) {
    LOG_ERR("SLC", "Invalid cache size for %s", path);
    file.close();
    Storage.remove(path);
    return false;
  }

  const int bytesRead = file.read(buffer, bufferSize);
  file.close();
  if (bytesRead != static_cast<int>(bufferSize)) {
    LOG_ERR("SLC", "Incomplete cache read for %s", path);
    return false;
  }

  return true;
}

bool SleepScreenCache::writePlaneFile(const char* path, const uint8_t* buffer, const uint32_t bufferSize) {
  FsFile file;
  if (!Storage.openFileForWrite("SLC", path, file)) {
    LOG_ERR("SLC", "Could not open cache file %s", path);
    return false;
  }

  const size_t bytesWritten = file.write(buffer, bufferSize);
  file.close();
  if (bytesWritten != bufferSize) {
    LOG_ERR("SLC", "Incomplete cache write for %s", path);
    Storage.remove(path);
    return false;
  }

  return true;
}

bool SleepGreyscaleStreamWriter::seekAndWrite(FsFile& file, const size_t offset, const uint8_t* data,
                                              const size_t length) const {
  if (data == nullptr || length == 0) {
    return false;
  }

  const size_t endOffset = offset + length;
  size_t fileSize = file.fileSize();
  if (endOffset > fileSize) {
    if (!file.seekSet(fileSize)) {
      return false;
    }
    uint8_t zeros[256] = {};
    size_t gap = endOffset - fileSize;
    while (gap > 0) {
      const size_t chunk = std::min(gap, sizeof(zeros));
      if (file.write(zeros, chunk) != chunk) {
        return false;
      }
      gap -= chunk;
    }
    fileSize = endOffset;
  }

  if (!file.seekSet(offset)) {
    return false;
  }
  return file.write(data, length) == length;
}

void SleepGreyscaleStreamWriter::closeWriteFiles() {
  if (lsbFile_) {
    lsbFile_.close();
  }
  if (msbFile_) {
    msbFile_.close();
  }
}

bool SleepGreyscaleStreamWriter::begin(const std::string& sourcePath, const uint32_t sourceFileSize,
                                       const uint32_t bufferSize, const uint16_t panelWidthBytes,
                                       const uint16_t panelHeight) {
  abort();

  sourcePath_ = sourcePath;
  sourceSize_ = sourceFileSize;
  if (sourceSize_ == 0 || bufferSize == 0 || panelWidthBytes == 0 || panelHeight == 0) {
    return false;
  }

  bufferSize_ = bufferSize;
  panelWidthBytes_ = panelWidthBytes;
  panelHeight_ = panelHeight;
  bwPath_ = SleepScreenCache::getCachePath(sourcePath, sourceSize_, ".raw");
  lsbPath_ = SleepScreenCache::getCachePath(sourcePath, sourceSize_, ".lsb.raw");
  msbPath_ = SleepScreenCache::getCachePath(sourcePath, sourceSize_, ".msb.raw");

  Storage.mkdir(SleepScreenCache::CACHE_DIR);
  SleepScreenCache::removeEntry(sourcePath, sourceSize_);

  if (!Storage.openFileForWrite("SLC", lsbPath_, lsbFile_) || !Storage.openFileForWrite("SLC", msbPath_, msbFile_)) {
    LOG_ERR("SLC", "Could not open greyscale sleep cache for write");
    closeWriteFiles();
    SleepScreenCache::removeEntry(sourcePath, sourceSize_);
    return false;
  }

  active_ = true;
  wroteStrip_ = false;
  return true;
}

bool SleepGreyscaleStreamWriter::appendStrip(const int yStart, const int numRows, const size_t stripBytes,
                                             const uint8_t* lsb, const uint8_t* msb) {
  if (!active_ || numRows <= 0 || stripBytes == 0 || lsb == nullptr || msb == nullptr) {
    return false;
  }

  const size_t offset = static_cast<size_t>(yStart) * panelWidthBytes_;
  if (offset + stripBytes > bufferSize_) {
    LOG_ERR("SLC", "Greyscale sleep cache strip out of bounds");
    return false;
  }

  if (!seekAndWrite(lsbFile_, offset, lsb, stripBytes)) {
    LOG_ERR("SLC", "Failed to write greyscale LSB strip at y=%d", yStart);
    return false;
  }
  if (!seekAndWrite(msbFile_, offset, msb, stripBytes)) {
    LOG_ERR("SLC", "Failed to write greyscale MSB strip at y=%d", yStart);
    return false;
  }

  wroteStrip_ = true;
  return true;
}

bool SleepGreyscaleStreamWriter::commitBw(const uint8_t* bwBuffer, const uint32_t bufferSize) {
  if (!active_ || bwBuffer == nullptr || bufferSize != bufferSize_ || !wroteStrip_) {
    return false;
  }

  closeWriteFiles();

  if (!SleepScreenCache::writePlaneFile(bwPath_.c_str(), bwBuffer, bufferSize)) {
    SleepScreenCache::removeEntry(sourcePath_, sourceSize_);
    active_ = false;
    return false;
  }

  LOG_DBG("SLC", "Saved greyscale cache: %s", bwPath_.c_str());
  return true;
}

void SleepGreyscaleStreamWriter::abort() {
  closeWriteFiles();
  if (!sourcePath_.empty() && sourceSize_ != 0) {
    SleepScreenCache::removeEntry(sourcePath_, sourceSize_);
  }
  sourcePath_.clear();
  sourceSize_ = 0;
  bufferSize_ = 0;
  panelWidthBytes_ = 0;
  panelHeight_ = 0;
  lsbPath_.clear();
  msbPath_.clear();
  bwPath_.clear();
  active_ = false;
  wroteStrip_ = false;
}

void SleepGreyscaleStreamWriter::flushToDisplay(const GfxRenderer& renderer) const {
  if (!wroteStrip_ || panelWidthBytes_ == 0 || panelHeight_ == 0) {
    return;
  }

  FsFile lsbFile;
  FsFile msbFile;
  if (!Storage.openFileForRead("SLC", lsbPath_, lsbFile) || !Storage.openFileForRead("SLC", msbPath_, msbFile)) {
    LOG_ERR("SLC", "Could not open greyscale sleep cache for display flush");
    return;
  }

  const size_t stripBytes = static_cast<size_t>(panelWidthBytes_) * STRIP_ROWS;
  auto scratch = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[stripBytes]);
  if (!scratch) {
    LOG_ERR("SLC", "OOM: greyscale sleep cache flush strip (%zu bytes)", stripBytes);
    lsbFile.close();
    msbFile.close();
    return;
  }

  const int lastStripIdx = (static_cast<int>(panelHeight_) - 1) / STRIP_ROWS;
  for (int stripIdx = 0; stripIdx <= lastStripIdx; stripIdx++) {
    const int yStart = stripIdx * STRIP_ROWS;
    const int rows = std::min(STRIP_ROWS, static_cast<int>(panelHeight_) - yStart);
    if (rows <= 0) {
      continue;
    }

    const size_t activeStripBytes = static_cast<size_t>(panelWidthBytes_) * static_cast<size_t>(rows);
    const size_t offset = static_cast<size_t>(yStart) * panelWidthBytes_;

    if (!lsbFile.seekSet(offset) || lsbFile.read(scratch.get(), activeStripBytes) != static_cast<int>(activeStripBytes)) {
      LOG_ERR("SLC", "Failed to read greyscale LSB strip at y=%d", yStart);
      break;
    }
    renderer.writeGrayscalePlaneStrip(true, scratch.get(), yStart, rows);

    if (!msbFile.seekSet(offset) || msbFile.read(scratch.get(), activeStripBytes) != static_cast<int>(activeStripBytes)) {
      LOG_ERR("SLC", "Failed to read greyscale MSB strip at y=%d", yStart);
      break;
    }
    renderer.writeGrayscalePlaneStrip(false, scratch.get(), yStart, rows);
  }

  lsbFile.close();
  msbFile.close();
}

bool SleepScreenCache::load(GfxRenderer& renderer, const std::string& sourcePath) {
  const uint32_t sourceSize = getSourceFileSize(sourcePath);
  if (sourceSize == 0) {
    return false;
  }

  const auto path = getCachePath(sourcePath, sourceSize, ".raw");
  const uint32_t bufferSize = display.getBufferSize();
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!readPlaneFile(path.c_str(), frameBuffer, bufferSize)) {
    return false;
  }

  LOG_DBG("SLC", "Loaded cache: %s", path.c_str());
  return true;
}

bool SleepScreenCache::loadGreyscale(GfxRenderer& renderer, const std::string& sourcePath) {
  const uint32_t sourceSize = getSourceFileSize(sourcePath);
  if (sourceSize == 0) {
    return false;
  }

  const uint32_t bufferSize = display.getBufferSize();
  const auto bwPath = getCachePath(sourcePath, sourceSize, ".raw");
  const auto lsbPath = getCachePath(sourcePath, sourceSize, ".lsb.raw");
  const auto msbPath = getCachePath(sourcePath, sourceSize, ".msb.raw");

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!readPlaneFile(bwPath.c_str(), frameBuffer, bufferSize)) {
    return false;
  }

  auto planeBuffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[bufferSize]);
  if (!planeBuffer) {
    LOG_ERR("SLC", "OOM: greyscale sleep cache plane (%u bytes)", bufferSize);
    return false;
  }

  if (!readPlaneFile(lsbPath.c_str(), planeBuffer.get(), bufferSize)) {
    return false;
  }
  display.copyGrayscaleLsbBuffers(planeBuffer.get());

  if (!readPlaneFile(msbPath.c_str(), planeBuffer.get(), bufferSize)) {
    return false;
  }
  display.copyGrayscaleMsbBuffers(planeBuffer.get());

  LOG_DBG("SLC", "Loaded greyscale cache: %s", bwPath.c_str());
  return true;
}

void SleepScreenCache::save(const GfxRenderer& renderer, const std::string& sourcePath) {
  Storage.mkdir(CACHE_DIR);

  const uint32_t sourceSize = getSourceFileSize(sourcePath);
  if (sourceSize == 0) {
    return;
  }

  const uint32_t bufferSize = display.getBufferSize();
  const auto path = getCachePath(sourcePath, sourceSize, ".raw");
  if (!writePlaneFile(path.c_str(), renderer.getFrameBuffer(), bufferSize)) {
    return;
  }

  LOG_DBG("SLC", "Saved cache: %s", path.c_str());
}

int SleepScreenCache::invalidateAll() {
  auto dir = Storage.open(CACHE_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  int count = 0;
  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    file.close();
    const auto fullPath = std::string(CACHE_DIR) + "/" + name;
    if (Storage.remove(fullPath.c_str())) {
      count++;
    }
  }
  dir.close();
  return count;
}
