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
  void setPreferredBuildId(const char* buildId);

  bool foundManifest() const;
  const char* getVersion() const;
  const char* getDownloadUrl() const;
  size_t getFirmwareSize() const;

 private:
  enum class LastKey : uint8_t {
    NONE,
    VERSION,
    BUILDS,
    ID,
    DOWNLOAD_URL,
    SIZE,
  };

  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_BUILDS_ARRAY,
    IN_BUILD_OBJECT,
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
  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t buildDepth;

  char preferredBuildId[16];
  char version[40];
  char downloadUrl[512];
  size_t firmwareSize;
  bool versionFound;
  bool downloadUrlFound;
  bool preferredBuildFound;

  char currentBuildId[16];
  char currentBuildDownloadUrl[512];
  size_t currentBuildFirmwareSize;
  bool currentBuildIdFound;
  bool currentBuildDownloadUrlFound;

  void resetCurrentBuild();
  void commitCurrentBuild();
};
