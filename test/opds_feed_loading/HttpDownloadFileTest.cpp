#include <gtest/gtest.h>

#include "DownloadFileAdapter.h"

namespace {
constexpr const char* destination = "/.crosspoint/opds-feed.tmp";

class HttpDownloadFile : public ::testing::Test {
 protected:
  void SetUp() override { downloadTest = DownloadTestState{}; }
  void TearDown() override {
    EXPECT_EQ(downloadTest.openFiles, 0);
    EXPECT_FALSE(downloadTest.removeWhileOpen);
  }
  HttpDownloader::DownloadError download() {
    return HttpDownloader::downloadToFile("https://library.example/opds", destination);
  }
  void expectRemovedAfterClose() {
    EXPECT_FALSE(downloadTest.files.contains(destination));
    ASSERT_GE(downloadTest.events.size(), 3u);
    EXPECT_EQ(downloadTest.events[downloadTest.events.size() - 3], "http-end");
    EXPECT_EQ(downloadTest.events[downloadTest.events.size() - 2], "close");
    EXPECT_EQ(downloadTest.events.back(), "remove");
    EXPECT_EQ(downloadTest.closeCalls, 1);
  }
};

TEST_F(HttpDownloadFile, SuccessfulFinalCloseKeepsAllWrittenBytes) {
  ASSERT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(downloadTest.files.at(destination), "first chunksecond chunk");
  EXPECT_EQ(downloadTest.closeCalls, 1);
  ASSERT_EQ(downloadTest.events.size(), 2u);
  EXPECT_EQ(downloadTest.events[0], "http-end");
  EXPECT_EQ(downloadTest.events[1], "close");
}

TEST_F(HttpDownloadFile, FailedFinalFlushReturnsFileErrorAfterSuccessfulWrites) {
  downloadTest.closeOk = false;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  EXPECT_EQ(downloadTest.writeCalls, 2);
  expectRemovedAfterClose();
}

class HttpDownloadFileExistingError : public HttpDownloadFile, public ::testing::WithParamInterface<int> {};
TEST_P(HttpDownloadFileExistingError, ExistingErrorTakesPrecedenceOverCloseFailure) {
  downloadTest.closeOk = false;
  downloadTest.result = GetParam();
  EXPECT_EQ(download(), GetParam());
  expectRemovedAfterClose();
}
INSTANTIATE_TEST_SUITE_P(Errors, HttpDownloadFileExistingError,
                         ::testing::Values(HttpDownloader::HTTP_ERROR, HttpDownloader::UNAUTHORIZED,
                                           HttpDownloader::ABORTED, HttpDownloader::FILE_ERROR));

TEST_F(HttpDownloadFile, OpenFailureDoesNotStartHttp) {
  downloadTest.openOk = false;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  EXPECT_EQ(downloadTest.transfers, 0);
  EXPECT_EQ(downloadTest.closeCalls, 0);
  EXPECT_FALSE(downloadTest.files.contains(destination));
}

TEST_F(HttpDownloadFile, ZeroBytesWithSuccessfulCloseReturnsHttpError) {
  downloadTest.chunks.clear();
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
  expectRemovedAfterClose();
}

TEST_F(HttpDownloadFile, CloseFailureTakesPrecedenceOverZeroBytes) {
  downloadTest.chunks.clear();
  downloadTest.closeOk = false;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  expectRemovedAfterClose();
}

TEST_F(HttpDownloadFile, PartialWriteRemovesIncompleteFile) {
  downloadTest.partialWriteAt = 2;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  EXPECT_EQ(downloadTest.writeCalls, 2);
  expectRemovedAfterClose();
}

TEST_F(HttpDownloadFile, RetryAfterCloseFailureReplacesStaleDataAndSucceeds) {
  downloadTest.closeOk = false;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  expectRemovedAfterClose();
  downloadTest.closeOk = true;
  downloadTest.files[destination] = "stale data";
  ASSERT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(downloadTest.files.at(destination), "first chunksecond chunk");
}

TEST_F(HttpDownloadFile, PreservesCredentialsHeadersProgressCancellationAndRedirectPolicy) {
  const bool cancel = false;
  const std::vector<HttpDownloader::Header> headers{{"Accept", "application/atom+xml"}};
  std::vector<std::pair<size_t, size_t>> progress;
  const auto result = HttpDownloader::downloadToFile(
      "https://library.example/opds", destination,
      [&](size_t count, size_t total) { progress.emplace_back(count, total); }, &cancel, "reader", "secret", headers,
      true);
  ASSERT_EQ(result, HttpDownloader::OK);
  EXPECT_EQ(downloadTest.username, "reader");
  EXPECT_EQ(downloadTest.password, "secret");
  EXPECT_EQ(downloadTest.headers, headers);
  EXPECT_EQ(downloadTest.cancelFlag, &cancel);
  EXPECT_TRUE(downloadTest.downgrade);
  ASSERT_EQ(progress.size(), 2u);
  EXPECT_EQ(progress.back().first, 23u);
  EXPECT_EQ(progress.back().second, 23u);
}
}  // namespace
