#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

class FirmwareManifestJsonParser {
 public:
  FirmwareManifestJsonParser();

  FirmwareManifestJsonParser(const FirmwareManifestJsonParser&) = delete;
  FirmwareManifestJsonParser& operator=(const FirmwareManifestJsonParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  bool foundManifest() const;
  const char* getVersion() const;
  const char* getDownloadUrl() const;
  size_t getFirmwareSize() const;

 private:
  enum class LastKey : uint8_t {
    NONE,
    VERSION,
    DOWNLOAD_URL,
    SIZE,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  StreamingJsonParser parser;
  LastKey lastKey;
  uint8_t depth;

  char version[40];
  char downloadUrl[512];
  size_t firmwareSize;
  bool versionFound;
  bool downloadUrlFound;
};
