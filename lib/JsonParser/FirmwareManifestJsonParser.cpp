#include "FirmwareManifestJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {
void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}
}  // namespace

FirmwareManifestJsonParser::FirmwareManifestJsonParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void FirmwareManifestJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  buildDepth = 0;
  preferredBuildId[0] = '\0';
  version[0] = '\0';
  downloadUrl[0] = '\0';
  firmwareSize = 0;
  versionFound = false;
  downloadUrlFound = false;
  preferredBuildFound = false;
  resetCurrentBuild();
}

void FirmwareManifestJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }
void FirmwareManifestJsonParser::setPreferredBuildId(const char* buildId) {
  if (!buildId) buildId = "";
  safeCopy(preferredBuildId, sizeof(preferredBuildId), buildId, strlen(buildId));
}

bool FirmwareManifestJsonParser::foundManifest() const { return versionFound && downloadUrlFound; }
const char* FirmwareManifestJsonParser::getVersion() const { return version; }
const char* FirmwareManifestJsonParser::getDownloadUrl() const { return downloadUrl; }
size_t FirmwareManifestJsonParser::getFirmwareSize() const { return firmwareSize; }

void FirmwareManifestJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 7 && memcmp(key, "version", 7) == 0) {
          self->lastKey = LastKey::VERSION;
        } else if (len == 6 && memcmp(key, "builds", 6) == 0) {
          self->lastKey = LastKey::BUILDS;
        } else if (len == 11 && memcmp(key, "downloadUrl", 11) == 0) {
          self->lastKey = LastKey::DOWNLOAD_URL;
        } else if (len == 4 && memcmp(key, "size", 4) == 0) {
          self->lastKey = LastKey::SIZE;
        } else {
          self->lastKey = LastKey::NONE;
        }
      }
      break;
    case Position::IN_BUILD_OBJECT:
      if (self->buildDepth == 1) {
        if (len == 2 && memcmp(key, "id", 2) == 0) {
          self->lastKey = LastKey::ID;
        } else if (len == 11 && memcmp(key, "downloadUrl", 11) == 0) {
          self->lastKey = LastKey::DOWNLOAD_URL;
        } else if (len == 4 && memcmp(key, "size", 4) == 0) {
          self->lastKey = LastKey::SIZE;
        } else {
          self->lastKey = LastKey::NONE;
        }
      }
      break;
    default:
      self->lastKey = LastKey::NONE;
      break;
  }
}

void FirmwareManifestJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->position == Position::TOP_LEVEL && self->depth == 1 && self->lastKey == LastKey::VERSION) {
    safeCopy(self->version, sizeof(self->version), value, len);
    self->versionFound = true;
  } else if (self->position == Position::TOP_LEVEL && self->depth == 1 && self->lastKey == LastKey::DOWNLOAD_URL &&
             !self->preferredBuildFound) {
    safeCopy(self->downloadUrl, sizeof(self->downloadUrl), value, len);
    self->downloadUrlFound = true;
  } else if (self->position == Position::IN_BUILD_OBJECT && self->buildDepth == 1 && self->lastKey == LastKey::ID) {
    safeCopy(self->currentBuildId, sizeof(self->currentBuildId), value, len);
    self->currentBuildIdFound = true;
  } else if (self->position == Position::IN_BUILD_OBJECT && self->buildDepth == 1 &&
             self->lastKey == LastKey::DOWNLOAD_URL) {
    safeCopy(self->currentBuildDownloadUrl, sizeof(self->currentBuildDownloadUrl), value, len);
    self->currentBuildDownloadUrlFound = true;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->position == Position::TOP_LEVEL && self->depth == 1 && self->lastKey == LastKey::SIZE &&
      !self->preferredBuildFound) {
    self->firmwareSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  } else if (self->position == Position::IN_BUILD_OBJECT && self->buildDepth == 1 && self->lastKey == LastKey::SIZE) {
    self->currentBuildFirmwareSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<FirmwareManifestJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnNull(void* ctx) {
  static_cast<FirmwareManifestJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);

  if (self->position == Position::IN_BUILDS_ARRAY) {
    self->position = Position::IN_BUILD_OBJECT;
    self->buildDepth = 1;
    self->resetCurrentBuild();
  } else if (self->position == Position::IN_BUILD_OBJECT) {
    ++self->buildDepth;
  } else {
    ++self->depth;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);

  if (self->position == Position::IN_BUILD_OBJECT) {
    if (self->buildDepth > 0) {
      --self->buildDepth;
    }
    if (self->buildDepth == 0) {
      self->commitCurrentBuild();
      self->position = Position::IN_BUILDS_ARRAY;
    }
  } else if (self->depth > 0) {
    --self->depth;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);

  if (self->position == Position::TOP_LEVEL && self->depth == 1 && self->lastKey == LastKey::BUILDS) {
    self->position = Position::IN_BUILDS_ARRAY;
  } else if (self->position == Position::IN_BUILD_OBJECT) {
    ++self->buildDepth;
  } else {
    ++self->depth;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);

  if (self->position == Position::IN_BUILDS_ARRAY) {
    self->position = Position::TOP_LEVEL;
  } else if (self->position == Position::IN_BUILD_OBJECT) {
    if (self->buildDepth > 0) {
      --self->buildDepth;
    }
  } else if (self->depth > 0) {
    --self->depth;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::resetCurrentBuild() {
  currentBuildId[0] = '\0';
  currentBuildDownloadUrl[0] = '\0';
  currentBuildFirmwareSize = 0;
  currentBuildIdFound = false;
  currentBuildDownloadUrlFound = false;
}

void FirmwareManifestJsonParser::commitCurrentBuild() {
  if (preferredBuildId[0] == '\0' || !currentBuildIdFound || !currentBuildDownloadUrlFound ||
      strcmp(currentBuildId, preferredBuildId) != 0) {
    resetCurrentBuild();
    return;
  }

  memcpy(downloadUrl, currentBuildDownloadUrl, sizeof(downloadUrl));
  firmwareSize = currentBuildFirmwareSize;
  downloadUrlFound = true;
  preferredBuildFound = true;
  resetCurrentBuild();
}
