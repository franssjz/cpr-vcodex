#include "OtaUpdater.h"

#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <cctype>
#include <cstring>

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_wifi.h"

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/franssjz/cpr-vcodex/releases/latest";

/*
 * When esp_crt_bundle.h is included here, Arduino's include path can resolve
 * the wrong header. Keep the upstream streaming OTA implementation but retain
 * the explicit declaration that already worked in CPR-vCodex.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

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

std::string buildUserAgent() { return std::string("CrossPoint-ESP32-") + currentVersionString(); }

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

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  const std::string userAgent = buildUserAgent();
  return esp_http_client_set_header(http_client, "User-Agent", userAgent.c_str());
}

size_t totalBytesReceived = 0;

esp_err_t event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  totalBytesReceived += event->data_len;
  LOG_DBG("OTA", "HTTP chunk: %d bytes (total: %zu)", event->data_len, totalBytesReceived);
  auto* parser = static_cast<ReleaseJsonParser*>(event->user_data);
  parser->feed(static_cast<const char*>(event->data), event->data_len);
  return ESP_OK;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  esp_err_t esp_err;
  ReleaseJsonParser releaseParser;

  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  esp_http_client_config_t client_config = {
      .url = latestReleaseUrl,
      .event_handler = event_handler,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .user_data = &releaseParser,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  totalBytesReceived = 0;
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    return INTERNAL_UPDATE_ERROR;
  }

  const std::string userAgent = buildUserAgent();
  esp_err = esp_http_client_set_header(client_handle, "User-Agent", userAgent.c_str());
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return HTTP_ERROR;
  }

  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_DBG("OTA", "Response received: %zu bytes total", totalBytesReceived);
  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No OTA firmware asset found");
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
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

  for (int index = 0; index < 4; ++index) {
    if (latest.parts[index] != currentVersion.parts[index]) {
      return latest.parts[index] > currentVersion.parts[index];
    }
  }

  const bool currentPreRelease = currentVersion.isRc || currentVersion.isDev;
  const bool latestPreRelease = latest.isRc || latest.isDev;
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

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t esp_err;

  esp_http_client_config_t client_config = {
      .url = otaUrl.c_str(),
      .timeout_ms = 15000,
      /* Default HTTP client buffer size 512 byte only
       * not sufficient to handle URL redirection cases or
       * parsing of large HTTP headers.
       */
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &client_config,
      .http_client_init_cb = http_client_set_header_cb,
  };

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_DBG("OTA", "HTTP OTA Begin Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  do {
    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    if (onProgress) onProgress(ctx);
    delay(100);  // TODO: should we replace this with something better?
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_perform Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    LOG_ERR("OTA", "esp_https_ota_is_complete_data_received Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_finish Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
