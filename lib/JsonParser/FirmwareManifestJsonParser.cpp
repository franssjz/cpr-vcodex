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
  lastKey = LastKey::NONE;
  depth = 0;
  version[0] = '\0';
  downloadUrl[0] = '\0';
  firmwareSize = 0;
  versionFound = false;
  downloadUrlFound = false;
}

void FirmwareManifestJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

bool FirmwareManifestJsonParser::foundManifest() const { return versionFound && downloadUrlFound; }
const char* FirmwareManifestJsonParser::getVersion() const { return version; }
const char* FirmwareManifestJsonParser::getDownloadUrl() const { return downloadUrl; }
size_t FirmwareManifestJsonParser::getFirmwareSize() const { return firmwareSize; }

void FirmwareManifestJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth != 1) {
    self->lastKey = LastKey::NONE;
    return;
  }

  if (len == 7 && memcmp(key, "version", 7) == 0) {
    self->lastKey = LastKey::VERSION;
  } else if (len == 11 && memcmp(key, "downloadUrl", 11) == 0) {
    self->lastKey = LastKey::DOWNLOAD_URL;
  } else if (len == 4 && memcmp(key, "size", 4) == 0) {
    self->lastKey = LastKey::SIZE;
  } else {
    self->lastKey = LastKey::NONE;
  }
}

void FirmwareManifestJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::VERSION) {
    safeCopy(self->version, sizeof(self->version), value, len);
    self->versionFound = true;
  } else if (self->depth == 1 && self->lastKey == LastKey::DOWNLOAD_URL) {
    safeCopy(self->downloadUrl, sizeof(self->downloadUrl), value, len);
    self->downloadUrlFound = true;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::SIZE) {
    self->firmwareSize = static_cast<size_t>(strtoul(value, nullptr, 10));
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
  ++self->depth;
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth > 0) {
    --self->depth;
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  ++self->depth;
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth > 0) {
    --self->depth;
  }
  self->lastKey = LastKey::NONE;
}
