#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <FirmwareManifestJsonParser.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
// clang-format on

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "version.h"

namespace {
constexpr char firmwareManifestUrl[] = "https://franssjz.github.io/cpr-vcodex/firmware/manifest.json";
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/franssjz/cpr-vcodex/releases/latest";

struct ParsedVersion {
  int parts[4] = {0, 0, 0, 0};
  bool parsed = false;
  bool isRc = false;
  bool isDev = false;
};

const char* currentVersionString() {
#ifdef VCODEX_VERSION
  return VCODEX_VERSION;
#else
  return CROSSPOINT_VERSION;
#endif
}

ParsedVersion parseVersion(const char* version) {
  ParsedVersion parsedVersion;
  if (!version) {
    return parsedVersion;
  }

  const char* cursor = version;
  while (*cursor && !std::isdigit(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }

  for (int index = 0; index < 4 && *cursor; ++index) {
    if (!std::isdigit(static_cast<unsigned char>(*cursor))) {
      break;
    }

    int value = 0;
    while (std::isdigit(static_cast<unsigned char>(*cursor))) {
      value = value * 10 + (*cursor - '0');
      ++cursor;
    }

    parsedVersion.parts[index] = value;
    parsedVersion.parsed = true;

    if (*cursor != '.') {
      break;
    }
    ++cursor;
  }

  parsedVersion.isRc = strstr(version, "-rc") != nullptr || strstr(version, ".rc") != nullptr;
  parsedVersion.isDev = strstr(version, "-dev") != nullptr || strstr(version, ".dev") != nullptr;
  return parsedVersion;
}

// The C3 X4/X3 binary is published without a board suffix (firmware.bin and
// <tag>.bin, matching every pre-existing release). Other boards get their own
// asset: firmware-<board>.bin / <tag>-<board>.bin (see ReleaseJsonParser).
bool isC3X4Board() { return board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0; }

void legacyAssetName(char* out, size_t outSize) {
  if (isC3X4Board()) {
    snprintf(out, outSize, "firmware.bin");
    return;
  }
  snprintf(out, outSize, "firmware-%.*s.bin", static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
}

// Stream the response straight into a parser as it arrives. Buffering the whole
// body in a std::string would add a growing allocation on top of the TLS
// session's heap during the fetch; with -fno-exceptions an OOM there aborts.
// fetchUrl handles the verified-https GET, redirects, and User-Agent.
template <typename Parser>
OtaUpdater::OtaUpdaterError performStreamingRequest(const char* url, Parser& parser, size_t& bytesReceived) {
  bytesReceived = 0;
  const bool ok = HttpDownloader::fetchUrl(url, [&parser, &bytesReceived](const uint8_t* data, size_t len) {
    bytesReceived += len;
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  return ok ? OtaUpdater::OK : OtaUpdater::HTTP_ERROR;
}

class WifiPowerSaveGuard {
 public:
  WifiPowerSaveGuard() { esp_wifi_set_ps(WIFI_PS_NONE); }
  ~WifiPowerSaveGuard() { esp_wifi_set_ps(WIFI_PS_MIN_MODEM); }
};

void notifyProgress(OtaUpdater::ProgressCallback callback, void* ctx) {
  if (callback) {
    callback(ctx);
  }
}

int hexNibble(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool sha256Matches(const std::string& expected, const uint8_t actual[32]) {
  if (expected.size() != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    const int high = hexNibble(expected[i * 2]);
    const int low = hexNibble(expected[i * 2 + 1]);
    if (high < 0 || low < 0 || actual[i] != static_cast<uint8_t>((high << 4) | low)) return false;
  }
  return true;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  expectedSha256.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  size_t bytesReceived = 0;
  // The auto-flash manifest only describes the C3 X4/X3 binary, so other boards
  // go straight to the GitHub release and its board-suffixed asset.
  if (isC3X4Board()) {
    FirmwareManifestJsonParser manifestParser;
    LOG_DBG("OTA", "Checking firmware manifest (current: %s)", currentVersionString());
    const OtaUpdaterError manifestResult = performStreamingRequest(firmwareManifestUrl, manifestParser, bytesReceived);
    LOG_DBG("OTA", "Manifest response received: %zu bytes total", bytesReceived);
    LOG_DBG("OTA", "Manifest parser result: manifest=%s", manifestParser.foundManifest() ? "yes" : "no");

    if (manifestResult != OK) {
      LOG_ERR("OTA", "Firmware manifest fetch failed");
      return manifestResult;
    }
    if (!manifestParser.foundManifest()) {
      LOG_ERR("OTA", "Firmware manifest missing required version, URL, or SHA-256");
      return JSON_PARSE_ERROR;
    }

    latestVersion = manifestParser.getVersion();
    otaUrl = manifestParser.getDownloadUrl();
    expectedSha256 = manifestParser.getFirmwareSha256();
    otaSize = manifestParser.getFirmwareSize();
    totalSize = otaSize;
    updateAvailable = true;
    LOG_DBG("OTA", "Found update via manifest: tag=%s size=%zu", latestVersion.c_str(), otaSize);
    LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
    return OK;
  }

  LOG_DBG("OTA", "Checking latest GitHub release (current: %s)", currentVersionString());

  ReleaseJsonParser releaseParser;
  // Each board updates from its own release asset. The parser prefers the
  // tag-named asset (<tag>.bin / <tag>-<board>.bin) and falls back to the legacy
  // name set here; releases without a firmware asset are ignored.
  char assetName[48];
  legacyAssetName(assetName, sizeof(assetName));
  releaseParser.setFirmwareAssetName(assetName);
  const OtaUpdaterError releaseResult = performStreamingRequest(latestReleaseUrl, releaseParser, bytesReceived);
  LOG_DBG("OTA", "Release response received: %zu bytes total", bytesReceived);
  LOG_DBG("OTA", "Release parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (releaseResult != OK) {
    LOG_ERR("OTA", "Release check fetch failed");
    return releaseResult;
  }

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_INF("OTA", "No %s asset in latest release", assetName);
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update via release: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }

  const auto currentVersion = parseVersion(currentVersionString());
  const auto latest = parseVersion(latestVersion.c_str());
  if (!currentVersion.parsed || !latest.parsed) {
    return false;
  }

  const bool currentPreRelease = currentVersion.isRc || currentVersion.isDev;
  const bool latestPreRelease = latest.isRc || latest.isDev;
  if (currentVersion.isDev && !latestPreRelease) {
    return true;
  }

  for (int index = 0; index < 4; ++index) {
    if (latest.parts[index] != currentVersion.parts[index]) {
      return latest.parts[index] > currentVersion.parts[index];
    }
  }

  if (currentPreRelease != latestPreRelease) {
    return !latestPreRelease && currentPreRelease;
  }

  if (currentVersion.isRc != latest.isRc) {
    return !latest.isRc && currentVersion.isRc;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // CrossPoint's direct-OTA design avoids a second copy on the SD card. For the
  // C3 image, the payload uses the proven pre-1.6.0.31 Arduino TLS transport to
  // avoid the CA-chain heap spike, while the SHA-256 comes from the separately
  // verified manifest. The inactive slot is never selected unless the digest,
  // image format, chip and embedded board tag all pass.
  if (isC3X4Board() && expectedSha256.size() != 64) {
    LOG_ERR("OTA", "Authenticated manifest SHA-256 missing");
    return JSON_PARSE_ERROR;
  }

  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition || (otaSize > 0 && otaSize > updatePartition->size)) {
    LOG_ERR("OTA", "No suitable OTA partition (size=%zu)", otaSize);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  const size_t beginSize = otaSize > 0 ? otaSize : OTA_SIZE_UNKNOWN;
  esp_err_t otaError = esp_ota_begin(updatePartition, beginSize, &otaHandle);
  if (otaError != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(otaError));
    return INTERNAL_UPDATE_ERROR;
  }

  WifiPowerSaveGuard wifiPowerSaveGuard;
  setProgress(0, otaSize);
  notifyProgress(onProgress, ctx);
  int lastReportedPercent = -1;
  uint8_t header[14] = {0};
  size_t headerLength = 0;
  bool wrongDevice = false;
  bool writeOk = true;
  bool sizeOk = true;
  board_tag::Scanner tagScanner;
  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts(&shaContext, 0);

  const auto consume = [this, otaHandle, onProgress, ctx, &lastReportedPercent, &header, &headerLength,
                        &wrongDevice, &writeOk, &sizeOk, &tagScanner, &shaContext](const uint8_t* data,
                                                                                 size_t len) {
    if (otaSize > 0 && (processedSize > otaSize || len > otaSize - processedSize)) {
      sizeOk = false;
      return false;
    }
    if (headerLength < sizeof(header)) {
      const size_t take = std::min(len, sizeof(header) - headerLength);
      std::memcpy(header + headerLength, data, take);
      headerLength += take;
      if (headerLength == sizeof(header)) {
        uint16_t imageChip = 0;
        std::memcpy(&imageChip, header + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          wrongDevice = true;
          return false;
        }
      }
    }
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      wrongDevice = true;
      return false;
    }
    mbedtls_sha256_update(&shaContext, data, len);
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      writeOk = false;
      return false;
    }
    processedSize += len;
    if (totalSize > 0) {
      const int percent =
          static_cast<int>(std::min<size_t>(100, static_cast<uint64_t>(processedSize) * 100 / totalSize));
      if (percent != lastReportedPercent) {
        lastReportedPercent = percent;
        notifyProgress(onProgress, ctx);
      }
    }
    return true;
  };

  HttpDownloader::DownloadError downloadResult;
  if (isC3X4Board()) {
    downloadResult = HttpDownloader::fetchOtaImage(otaUrl, consume);
  } else {
    downloadResult = HttpDownloader::fetchUrl(otaUrl, consume) ? HttpDownloader::OK : HttpDownloader::HTTP_ERROR;
  }

  uint8_t actualSha256[32];
  mbedtls_sha256_finish(&shaContext, actualSha256);
  mbedtls_sha256_free(&shaContext);

  if (wrongDevice) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }
  if (downloadResult != HttpDownloader::OK || !writeOk || !sizeOk || processedSize == 0 ||
      (otaSize > 0 && processedSize != otaSize)) {
    LOG_ERR("OTA", "Firmware transfer failed: http=%d write=%d size=%zu/%zu", downloadResult, writeOk,
            processedSize, otaSize);
    esp_ota_abort(otaHandle);
    return writeOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }
  if (!expectedSha256.empty() && !sha256Matches(expectedSha256, actualSha256)) {
    LOG_ERR("OTA", "Firmware SHA-256 does not match verified manifest");
    esp_ota_abort(otaHandle);
    return HTTP_ERROR;
  }

  otaError = esp_ota_end(otaHandle);
  if (otaError != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(otaError));
    return INTERNAL_UPDATE_ERROR;
  }
  otaError = esp_ota_set_boot_partition(updatePartition);
  if (otaError != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(otaError));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
