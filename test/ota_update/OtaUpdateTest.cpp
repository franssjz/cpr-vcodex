#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "FakeOtaPlatform.h"
#include "FirmwareBoardTag.h"
#include "HttpDownloader.h"
#include "OtaUpdater.h"
#include "version.h"

#ifndef TEST_INSTALLED_VERSION
#define TEST_INSTALLED_VERSION "1.6.0.36"
#endif
const char CPR_CROSSPOINT_VERSION[] = TEST_INSTALLED_VERSION;

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
std::vector<std::string> requestedUrls;
std::string imageUrl;
const esp_partition_t* begunPartition;
const esp_partition_t* selectedPartition;
size_t begunSize;
std::vector<std::string> flashEvents;
}  // namespace

const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  return hasPartition ? &partition : nullptr;
}
esp_err_t esp_ota_begin(const esp_partition_t* target, size_t size, esp_ota_handle_t* handle) {
  ++begins;
  begunPartition = target;
  begunSize = size;
  flashEvents.emplace_back("begin");
  *handle = 1;
  return beginError;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) {
  ++writes;
  flashEvents.emplace_back("write");
  return writeError;
}
esp_err_t esp_ota_abort(esp_ota_handle_t) {
  ++aborts;
  flashEvents.emplace_back("abort");
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t) {
  ++ends;
  flashEvents.emplace_back("end");
  return endError;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* target) {
  ++switches;
  selectedPartition = target;
  flashEvents.emplace_back("activate");
  return switchError;
}
namespace firmware_flash {
uint16_t runningPartitionChipId() { return 5; }  // ESP32-C3
}  // namespace firmware_flash

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& consume, const std::string&,
                              const std::string&) {
  ++manifestCalls;
  requestedUrls.push_back(url);
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
HttpDownloader::DownloadError HttpDownloader::fetchOtaImage(const std::string& url, const DataCallback& consume,
                                                            ProgressCallback) {
  imageUrl = url;
  for (size_t offset = 0; offset < deliveredSize; offset += chunkSize) {
    if (!consume(image.data() + offset, std::min(chunkSize, deliveredSize - offset))) return FILE_ERROR;
  }
  return downloadOk ? OK : HTTP_ERROR;
}

class OtaUpdate : public testing::Test {
 protected:
  OtaUpdater updater;
  void SetUp() override {
    partition = {};
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
    version = "1.6.0.39-cpr-vcodex";
    manifestOverride.clear();
    manifestCalls = 0;
    failPrimaryManifest = false;
    requestedUrls.clear();
    imageUrl.clear();
    begunPartition = selectedPartition = nullptr;
    begunSize = 0;
    flashEvents.clear();
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
  for (const char* published : {"1.6.0.33-cpr-vcodex", TEST_INSTALLED_VERSION}) {
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

TEST_F(OtaUpdate, UsesForkManifestFallbackAndItsExactImageUrl) {
  failPrimaryManifest = true;
  EXPECT_EQ(install(), OtaUpdater::OK);
  ASSERT_EQ(requestedUrls.size(), 2u);
  EXPECT_EQ(requestedUrls[0], "https://franssjz.github.io/cpr-vcodex/firmware/manifest.json");
  EXPECT_EQ(requestedUrls[1],
            "https://raw.githubusercontent.com/franssjz/cpr-vcodex/master/docs/firmware/manifest.json");
  EXPECT_EQ(imageUrl, "https://example.com/firmware.bin");
  EXPECT_EQ(std::string(board_tag::boardName(), board_tag::boardNameLen()), "x4");
}

TEST_F(OtaUpdate, ReleaseAfterDevelopmentBuildRemainsReachable) {
  // The same numeric stable release supersedes dev35, even when published earlier.
  version = "1.6.0.38-cpr-vcodex";
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  version = "1.6.0.39-cpr-vcodex";
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  version = "1.6.5.1-cpr-vcodex";
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
}

TEST_F(OtaUpdate, FailedRecheckCannotReuseAnEarlierCandidate) {
  ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  manifestOk = false;
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::HTTP_ERROR);
  EXPECT_EQ(updater.installUpdate(nullptr, nullptr), OtaUpdater::UPDATE_OLDER_ERROR);
  EXPECT_EQ(begins, 0);
  EXPECT_TRUE(imageUrl.empty());
}

TEST_F(OtaUpdate, RetryAfterInterruptedTransferResetsProgressAndActivatesOnce) {
  deliveredSize = 12345;
  EXPECT_NE(install(), OtaUpdater::OK);
  EXPECT_EQ(switches, 0);
  deliveredSize = advertisedSize;
  EXPECT_EQ(install(), OtaUpdater::OK);
  EXPECT_EQ(updater.getProcessedSize(), advertisedSize);
  EXPECT_EQ(aborts, 1);
  EXPECT_EQ(switches, 1);
}

TEST_F(OtaUpdate, InvalidManifestNeverTouchesFlash) {
  for (const auto* invalid :
       {"{\"version\":\"1.6.0.39\",\"downloadUrl\":\"https://example.com/f.bin\",\"size\":65536}",
        "{\"version\":\"1.6.0.39\",\"downloadUrl\":\"http://example.com/f.bin\",\"size\":65536,\"sha256\":\"bad\"}",
        "{\"version\":\"1.6.0.39\",\"downloadUrl\":\"https://example.com/f.bin\",\"size\":-1,\"sha256\":\"bad\"}"}) {
    manifestOverride = invalid;
    EXPECT_EQ(install(), OtaUpdater::JSON_PARSE_ERROR);
    EXPECT_EQ(begins, 0);
    EXPECT_EQ(switches, 0);
  }
}

TEST_F(OtaUpdate, OtherBoardTagsCannotBecomeBootTargets) {
  for (const auto* board : {"x4c", "x4pro", "papermono"}) {
    SetUp();
    const std::string tag = std::string("CROSSPOINT-BOARD-V1:") + board + ";";
    std::copy(tag.begin(), tag.end(), image.begin() + 1020);
    EXPECT_EQ(install(), OtaUpdater::WRONG_DEVICE_ERROR) << board;
    EXPECT_EQ(switches, 0);
    EXPECT_EQ(ends, 0);
  }
}

struct OtaLayout {
  size_t size;
  uint32_t address;
};

class OtaLayoutUpdate : public OtaUpdate, public testing::WithParamInterface<OtaLayout> {};

TEST_P(OtaLayoutUpdate, UsesRuntimeInactivePartitionInEitherDirection) {
  partition.size = GetParam().size;
  partition.address = GetParam().address;
  advertisedSize = deliveredSize = partition.size;
  image.resize(deliveredSize);
  ASSERT_EQ(install(), OtaUpdater::OK);
  EXPECT_EQ(begunPartition, &partition);
  EXPECT_EQ(selectedPartition, &partition);
  EXPECT_EQ(begunSize, partition.size);
  EXPECT_EQ(updater.getProcessedSize(), partition.size);
  ASSERT_GE(flashEvents.size(), 3u);
  EXPECT_EQ(flashEvents.front(), "begin");
  EXPECT_EQ(flashEvents[flashEvents.size() - 2], "end");
  EXPECT_EQ(flashEvents.back(), "activate");
}

TEST_P(OtaLayoutUpdate, OneByteOverActualSlotIsRejectedBeforeErase) {
  partition.size = GetParam().size;
  partition.address = GetParam().address;
  advertisedSize = partition.size + 1;
  EXPECT_EQ(install(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_TRUE(flashEvents.empty());
}

INSTANTIATE_TEST_SUITE_P(X4AndX3, OtaLayoutUpdate,
                         testing::Values(OtaLayout{0x640000, 0x10000}, OtaLayout{0x640000, 0x650000},
                                         OtaLayout{0x770000, 0x10000}, OtaLayout{0x770000, 0x780000}));
