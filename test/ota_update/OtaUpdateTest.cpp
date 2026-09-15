#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "FakeOtaPlatform.h"
#include "HttpDownloader.h"
#include "OtaUpdater.h"
#include "version.h"

const char CPR_CROSSPOINT_VERSION[] = "1.6.0.36";

namespace {
esp_partition_t partition;
bool hasPartition, manifestOk, downloadOk;
int begins, aborts, ends, switches, writes;
int beginError, writeError, endError, switchError;
size_t advertisedSize, deliveredSize, chunkSize;
std::vector<uint8_t> image;
std::string version;
std::string manifestOverride;
int manifestCalls;
bool failPrimaryManifest;
}  // namespace

const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  return hasPartition ? &partition : nullptr;
}
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* handle) {
  ++begins;
  *handle = 1;
  return beginError;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) {
  ++writes;
  return writeError;
}
esp_err_t esp_ota_abort(esp_ota_handle_t) {
  ++aborts;
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t) {
  ++ends;
  return endError;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
  ++switches;
  return switchError;
}
namespace firmware_flash {
uint16_t runningPartitionChipId() { return 5; }  // ESP32-C3
}  // namespace firmware_flash

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& consume, const std::string&,
                              const std::string&) {
  ++manifestCalls;
  if (failPrimaryManifest && url.find("raw.githubusercontent.com") == std::string::npos) return false;
  if (!manifestOk) return false;
  const std::string manifest =
      manifestOverride.empty()
          ? "{\"version\":\"" + version +
                "\",\"downloadUrl\":\"https://example.com/firmware.bin\",\"size\":" + std::to_string(advertisedSize) +
                ",\"sha256\":\"" + std::string(64, 'a') + "\"}"
          : manifestOverride;
  return consume(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size());
}
HttpDownloader::DownloadError HttpDownloader::fetchOtaImage(const std::string&, const DataCallback& consume,
                                                            ProgressCallback) {
  for (size_t offset = 0; offset < deliveredSize; offset += chunkSize) {
    if (!consume(image.data() + offset, std::min(chunkSize, deliveredSize - offset))) return FILE_ERROR;
  }
  return downloadOk ? OK : HTTP_ERROR;
}

class OtaUpdate : public testing::Test {
 protected:
  OtaUpdater updater;
  void SetUp() override {
    partition.size = 6553600;
    hasPartition = manifestOk = downloadOk = true;
    begins = aborts = ends = switches = writes = 0;
    beginError = writeError = endError = switchError = 0;
    advertisedSize = deliveredSize = 65536;
    chunkSize = 1024;
    testDigestByte = 0xaa;
    image.assign(65537, 0);
    image[0] = 0xe9;
    image[12] = 5;
    const std::string tag = "CROSSPOINT-BOARD-V1:x4;";
    std::copy(tag.begin(), tag.end(), image.begin() + 1020);
    version = "1.6.0.38-cpr-vcodex";
    manifestOverride.clear();
    manifestCalls = 0;
    failPrimaryManifest = false;
  }
  OtaUpdater::OtaUpdaterError install() {
    if (updater.checkForUpdate() != OtaUpdater::OK) return OtaUpdater::JSON_PARSE_ERROR;
    return updater.installUpdate(nullptr, nullptr);
  }
};

TEST_F(OtaUpdate, ActivatesOnlyAfterCompleteVerifiedImage) {
  EXPECT_EQ(install(), OtaUpdater::OK);
  EXPECT_EQ(updater.getProcessedSize(), advertisedSize);
  EXPECT_EQ(aborts, 0);
  EXPECT_EQ(ends, 1);
  EXPECT_EQ(switches, 1);
}
TEST_F(OtaUpdate, HandlesHeaderAndBoardTagSplitAcrossSingleBytes) {
  chunkSize = 1;
  EXPECT_EQ(install(), OtaUpdater::OK);
  EXPECT_EQ(switches, 1);
}
TEST_F(OtaUpdate, NoBytesOrInterruptedTransferNeverActivates) {
  for (const auto bytes : {size_t(0), size_t(100), size_t(65536)}) {
    deliveredSize = bytes;
    downloadOk = false;
    EXPECT_NE(install(), OtaUpdater::OK);
    EXPECT_EQ(switches, 0);
    EXPECT_EQ(ends, 0);
  }
  EXPECT_EQ(aborts, 3);
}
TEST_F(OtaUpdate, TruncatedOrOversizedBodyNeverActivates) {
  for (const auto bytes : {size_t(65535), size_t(65537)}) {
    deliveredSize = bytes;
    EXPECT_NE(install(), OtaUpdater::OK);
    EXPECT_EQ(switches, 0);
  }
  EXPECT_EQ(aborts, 2);
}
TEST_F(OtaUpdate, WrongDigestNeverActivates) {
  testDigestByte = 0xbb;
  EXPECT_NE(install(), OtaUpdater::OK);
  EXPECT_EQ(aborts, 1);
  EXPECT_EQ(ends, 0);
  EXPECT_EQ(switches, 0);
}
TEST_F(OtaUpdate, WrongChipNeverActivates) {
  image[12] = 9;  // ESP32-S3
  EXPECT_EQ(install(), OtaUpdater::WRONG_DEVICE_ERROR);
  EXPECT_EQ(switches, 0);
  EXPECT_EQ(writes, 0);
}
TEST_F(OtaUpdate, WrongBoardNeverActivates) {
  const std::string wrong = "CROSSPOINT-BOARD-V1:x4pro;";
  std::copy(wrong.begin(), wrong.end(), image.begin() + 1020);
  EXPECT_EQ(install(), OtaUpdater::WRONG_DEVICE_ERROR);
  EXPECT_EQ(switches, 0);
}
TEST_F(OtaUpdate, InvalidTargetOrSizeDoesNotBeginWriting) {
  hasPartition = false;
  EXPECT_NE(install(), OtaUpdater::OK);
  hasPartition = true;
  advertisedSize = partition.size + 1;
  EXPECT_NE(install(), OtaUpdater::OK);
  advertisedSize = 100;
  EXPECT_NE(install(), OtaUpdater::OK);
  EXPECT_EQ(begins, 0);
  EXPECT_EQ(switches, 0);
}
TEST_F(OtaUpdate, FlashFailuresNeverReportSuccess) {
  beginError = -1;
  EXPECT_NE(install(), OtaUpdater::OK);
  EXPECT_EQ(writes, 0);
  beginError = 0;
  writeError = -1;
  EXPECT_NE(install(), OtaUpdater::OK);
  EXPECT_EQ(aborts, 1);
  writeError = 0;
  endError = -1;
  EXPECT_NE(install(), OtaUpdater::OK);
  EXPECT_EQ(switches, 0);
  endError = 0;
  switchError = -1;
  EXPECT_NE(install(), OtaUpdater::OK);
}
TEST_F(OtaUpdate, FailedManifestOrDowngradeNeverWrites) {
  manifestOk = false;
  EXPECT_NE(install(), OtaUpdater::OK);
  manifestOk = true;
  version = "1.6.0.32-cpr-vcodex";
  EXPECT_EQ(install(), OtaUpdater::UPDATE_OLDER_ERROR);
  EXPECT_EQ(begins, 0);
  EXPECT_EQ(switches, 0);
}

TEST_F(OtaUpdate, CurrentOrNewerInstalledVersionIsSuccessfulCheckWithoutInstallation) {
  for (const char* published : {"1.6.0.33-cpr-vcodex", "1.6.0.36-cpr-vcodex", "1.6.0.36"}) {
    version = published;
    EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    EXPECT_EQ(updater.getLatestVersion(), published);
    EXPECT_FALSE(updater.isUpdateNewer());
    EXPECT_EQ(updater.installUpdate(nullptr, nullptr), OtaUpdater::UPDATE_OLDER_ERROR);
  }
  EXPECT_EQ(begins, 0);
}

TEST_F(OtaUpdate, FullPublishedPagesManifestCanBeChecked) {
  std::ifstream file(TEST_REPO_ROOT "/docs/firmware/manifest.json");
  ASSERT_TRUE(file.is_open());
  manifestOverride.assign(std::istreambuf_iterator<char>(file), {});
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_FALSE(updater.getLatestVersion().empty());
  EXPECT_GT(updater.getOtaSize(), 0u);
  EXPECT_EQ(begins, 0);
}

TEST_F(OtaUpdate, RetriesPublishedManifestOnIndependentHttpsHost) {
  failPrimaryManifest = true;
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  EXPECT_EQ(manifestCalls, 2);
  EXPECT_EQ(begins, 0);
  manifestOk = false;
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::HTTP_ERROR);
  EXPECT_FALSE(updater.isUpdateNewer());
  EXPECT_TRUE(updater.getLatestVersion().empty());
}
