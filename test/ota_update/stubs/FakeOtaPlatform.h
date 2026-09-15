#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

using esp_err_t = int;
using esp_ota_handle_t = unsigned;
constexpr esp_err_t ESP_OK = 0;
constexpr size_t OTA_SIZE_UNKNOWN = SIZE_MAX;
struct esp_partition_t {
  size_t size = 6553600;
};
constexpr int WIFI_PS_NONE = 0;
constexpr int WIFI_PS_MIN_MODEM = 1;
inline int esp_wifi_set_ps(int) { return ESP_OK; }
inline const char* esp_err_to_name(int) { return "fake error"; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);

// Inject the digest result to exercise activation policy. These tests do not
// implement or validate cryptography; production uses mbedTLS on the device.
inline uint8_t testDigestByte = 0xaa;
struct mbedtls_sha256_context {};
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return 0; }
inline int mbedtls_sha256_update(mbedtls_sha256_context*, const uint8_t*, size_t) { return 0; }
inline int mbedtls_sha256_finish(mbedtls_sha256_context*, uint8_t* digest) {
  std::memset(digest, testDigestByte, 32);
  return 0;
}
