#include "FirmwareManifestJsonParser.h"

#include <cctype>
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
  firmwareSha256[0] = '\0';
  firmwareSize = 0;
  versionFound = false;
  downloadUrlFound = false;
  sha256Found = false;
  rootComplete = false;
}

void FirmwareManifestJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

bool FirmwareManifestJsonParser::foundManifest() const {
  return rootComplete && depth == 0 && !parser.hasError() && versionFound && downloadUrlFound && sha256Found &&
         firmwareSize > 0;
}
const char* FirmwareManifestJsonParser::getVersion() const { return version; }
const char* FirmwareManifestJsonParser::getDownloadUrl() const { return downloadUrl; }
size_t FirmwareManifestJsonParser::getFirmwareSize() const { return firmwareSize; }
const char* FirmwareManifestJsonParser::getFirmwareSha256() const { return firmwareSha256; }

void FirmwareManifestJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth != 1) {
    self->lastKey = LastKey::NONE;
    return;
  }

  if (len == 7 && memcmp(key, "version", 7) == 0) {
    self->lastKey = LastKey::VERSION;
    self->versionFound = false;
  } else if (len == 11 && memcmp(key, "downloadUrl", 11) == 0) {
    self->lastKey = LastKey::DOWNLOAD_URL;
    self->downloadUrlFound = false;
  } else if (len == 4 && memcmp(key, "size", 4) == 0) {
    self->lastKey = LastKey::SIZE;
    self->firmwareSize = 0;
  } else if (len == 6 && memcmp(key, "sha256", 6) == 0) {
    self->lastKey = LastKey::SHA256;
    self->sha256Found = false;
  } else {
    self->lastKey = LastKey::NONE;
  }
}

void FirmwareManifestJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::VERSION) {
    safeCopy(self->version, sizeof(self->version), value, len);
    self->versionFound = len > 0 && len < sizeof(self->version);
  } else if (self->depth == 1 && self->lastKey == LastKey::DOWNLOAD_URL) {
    safeCopy(self->downloadUrl, sizeof(self->downloadUrl), value, len);
    self->downloadUrlFound = len > 8 && len < sizeof(self->downloadUrl) && memcmp(value, "https://", 8) == 0;
  } else if (self->depth == 1 && self->lastKey == LastKey::SHA256) {
    self->firmwareSha256[0] = '\0';
    self->sha256Found = false;
    if (len == 64) {
      bool valid = true;
      for (size_t i = 0; i < len; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
          valid = false;
          break;
        }
      }
      if (valid) {
        safeCopy(self->firmwareSha256, sizeof(self->firmwareSha256), value, len);
        self->sha256Found = true;
      }
    }
  }
  self->lastKey = LastKey::NONE;
}

void FirmwareManifestJsonParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<FirmwareManifestJsonParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::SIZE) {
    // Bound to the device's 32-bit size_t even in 64-bit host tests. Reject
    // negatives, fractions, exponents and overflow instead of truncating.
    uint32_t size = 0;
    for (size_t i = 0; i < len; ++i) {
      if (value[i] < '0' || value[i] > '9' || size > (UINT32_MAX - (value[i] - '0')) / 10) {
        self->lastKey = LastKey::NONE;
        return;
      }
      size = size * 10 + (value[i] - '0');
    }
    self->firmwareSize = size;
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
    if (self->depth == 0) self->rootComplete = true;
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
