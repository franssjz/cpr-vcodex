#include "QrUtils.h"

#include <Utf8.h>
#include <qrcode.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "Logging.h"

namespace {

constexpr int QUIET_ZONE_MODULES = 4;
constexpr int MIN_READABLE_MODULE_PIXELS = 3;
constexpr int MAX_READABLE_VERSION = 20;

// QR Code byte-mode capacities for ECC_LOW, versions 1..40.
// The QR library can auto-select numeric/alphanumeric mode, but page excerpts are
// usually byte mode; using byte capacities keeps version selection conservative.
constexpr uint16_t BYTE_CAPACITY_ECC_LOW[] = {
    17,   32,   53,   78,   106,  134,  154,  192,  230,  271,
    321,  367,  425,  458,  520,  586,  644,  718,  792,  858,
    929,  1003, 1091, 1171, 1273, 1367, 1465, 1528, 1628, 1732,
    1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953,
};

int getMaxReadableVersion(const Rect& bounds) {
  const int maxDim = std::min(bounds.width, bounds.height);
  const int maxModules = maxDim / MIN_READABLE_MODULE_PIXELS - 2 * QUIET_ZONE_MODULES;
  const int version = (maxModules - 17) / 4;
  return std::max(1, std::min(MAX_READABLE_VERSION, version));
}

int getSmallestVersionForLength(const size_t len, const int maxVersion) {
  for (int version = 1; version <= maxVersion; ++version) {
    if (len <= BYTE_CAPACITY_ECC_LOW[version - 1]) {
      return version;
    }
  }
  return maxVersion;
}

void fillRectRaw(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                 const bool state) {
  for (int fillY = y; fillY < y + height; ++fillY) {
    for (int fillX = x; fillX < x + width; ++fillX) {
      renderer.drawPixelDirect(fillX, fillY, state);
    }
  }
}

}  // namespace

void QrUtils::drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload) {
  size_t len = textPayload.length();
  std::string truncated;

  const int maxVersion = getMaxReadableVersion(bounds);
  int version = getSmallestVersionForLength(len, maxVersion);
  const size_t maxCapacity = BYTE_CAPACITY_ECC_LOW[version - 1];

  const char* payload = textPayload.c_str();
  if (len > maxCapacity) {
    len = utf8SafeTruncateBuffer(textPayload.c_str(), static_cast<int>(maxCapacity));
    truncated = textPayload.substr(0, len);
    payload = truncated.c_str();
    version = maxVersion;
  }

  uint32_t bufferSize = qrcode_getBufferSize(version);
  std::unique_ptr<uint8_t[]> qrcodeBytes(new (std::nothrow) uint8_t[bufferSize]);
  if (!qrcodeBytes) {
    LOG_ERR("QR", "Failed to allocate QR buffer for version %d", version);
    return;
  }

  QRCode qrcode;
  // Initialize the QR code. We use ECC_LOW for max capacity.
  int8_t res =
      qrcode_initBytes(&qrcode, qrcodeBytes.get(), version, ECC_LOW, reinterpret_cast<uint8_t*>(const_cast<char*>(payload)),
                       static_cast<uint16_t>(len));

  if (res == 0) {
    const int maxDim = std::min(bounds.width, bounds.height);
    const int totalModules = qrcode.size + 2 * QUIET_ZONE_MODULES;

    int px = maxDim / totalModules;
    if (px < 1) px = 1;

    const int qrDisplaySize = totalModules * px;
    const int xOff = bounds.x + (bounds.width - qrDisplaySize) / 2;
    const int yOff = bounds.y + (bounds.height - qrDisplaySize) / 2;
    fillRectRaw(renderer, xOff, yOff, qrDisplaySize, qrDisplaySize, false);

    const int moduleXOff = xOff + QUIET_ZONE_MODULES * px;
    const int moduleYOff = yOff + QUIET_ZONE_MODULES * px;
    for (uint8_t cy = 0; cy < qrcode.size; cy++) {
      for (uint8_t cx = 0; cx < qrcode.size; cx++) {
        if (qrcode_getModule(&qrcode, cx, cy)) {
          fillRectRaw(renderer, moduleXOff + px * cx, moduleYOff + px * cy, px, px, true);
        }
      }
    }
  } else {
    LOG_ERR("QR", "Failed to generate QR Code version %d for %u bytes", version, static_cast<unsigned>(len));
  }
}
