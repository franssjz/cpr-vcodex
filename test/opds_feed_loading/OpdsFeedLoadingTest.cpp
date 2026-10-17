#include <gtest/gtest.h>

#include "FeedLoadingAdapter.h"

namespace {
std::string makeFeed(size_t books = 1, bool pagination = false) {
  std::string xml = R"(<feed xmlns="http://www.w3.org/2005/Atom">)";
  if (pagination) {
    xml += R"(<link rel="previous" href="?page=1"/>)";
    xml += R"(<link rel="next" href="?page=3"/>)";
    xml += R"(<link rel="search" type="application/atom+xml" href="/search?q={searchTerms}"/>)";
  }
  for (size_t index = 0; index < books; ++index) {
    xml += "<entry><id>book-" + std::to_string(index) + "</id><title>Book " + std::to_string(index);
    xml +=
        R"(</title><author><name>A Writer</name></author><link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="books/)";
    xml += std::to_string(index) + ".epub\"/></entry>";
  }
  return xml + "</feed>";
}

class OpdsFeedLoading : public ::testing::Test {
 protected:
  OpdsBookBrowserActivity activity;

  void SetUp() override {
    feedTest = FeedTestState{};
    feedTest.response = makeFeed();
  }
  void TearDown() override {
    EXPECT_EQ(feedTest.liveParsers, 0);
    EXPECT_EQ(feedTest.openReaders, 0);
    EXPECT_FALSE(feedTest.httpActive);
    EXPECT_EQ(feedTest.renderLockDepth, 0);
    EXPECT_FALSE(feedTest.parserDuringHttp);
    EXPECT_FALSE(feedTest.flushWithOpenFile);
    EXPECT_FALSE(feedTest.removeWithOpenFile);
    EXPECT_FALSE(feedTest.unlockedCacheRelease);
    EXPECT_FALSE(feedTest.unlockedCatalogRelease);
    EXPECT_FALSE(feedTest.waitWithRenderLock);
  }
  void expectClean() {
    EXPECT_FALSE(feedTest.files.contains(feedTempPath));
    EXPECT_EQ(feedTest.liveParsers, 0);
    EXPECT_EQ(feedTest.openReaders, 0);
  }
  void expectFailure(StrId error) {
    EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::ERROR);
    EXPECT_EQ(activity.errorMessage, tr(error));
    EXPECT_TRUE(activity.entries.empty());
    EXPECT_TRUE(activity.rowItems.empty());
    expectClean();
  }
  size_t eventIndex(const std::string& event) {
    const auto it = std::find(feedTest.events.begin(), feedTest.events.end(), event);
    EXPECT_NE(it, feedTest.events.end()) << event;
    return static_cast<size_t>(it - feedTest.events.begin());
  }
};

TEST_F(OpdsFeedLoading, ParsesOnlyAfterHttpHasClosedAndPreservesCredentials) {
  activity.startBrowse();

  ASSERT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  ASSERT_EQ(activity.entries.size(), 1u);
  EXPECT_EQ(activity.entries[0].title, "Book 0");
  EXPECT_EQ(activity.entries[0].author, "A Writer");
  EXPECT_EQ(activity.entries[0].href, "books/0.epub");
  EXPECT_EQ(activity.entries[0].type, OpdsEntryType::BOOK);
  ASSERT_EQ(activity.rowItems.size(), 1u);
  EXPECT_STREQ(activity.rowItems[0].label, "Book 0");
  EXPECT_STREQ(activity.rowItems[0].subtitle, "A Writer");
  EXPECT_EQ(feedTest.requestedUrl, "https://library.example/opds");
  EXPECT_EQ(feedTest.username, "reader");
  EXPECT_EQ(feedTest.password, "secret");
  EXPECT_EQ(feedTest.destination, feedTempPath);
  EXPECT_LT(eventIndex("http-end"), eventIndex("parser-create"));
  EXPECT_LT(eventIndex("file-close"), eventIndex("parser-flush"));
  expectClean();
}

TEST_F(OpdsFeedLoading, ReleasesOldCatalogBeforeRenderingAndAllocatingNetworkResources) {
  activity.entries.resize(62);
  activity.rowItems.resize(62);
  activity.searchTemplate = "old search";
  activity.prevRowPresent = activity.nextRowPresent = true;
  activity.nav.selected = 61;
  feedTest.downloadResult = HttpDownloader::HTTP_ERROR;

  activity.startBrowse();

  expectFailure(STR_FETCH_FEED_FAILED);
  EXPECT_EQ(activity.entries.capacity(), 0u);
  EXPECT_EQ(activity.rowItems.capacity(), 0u);
  EXPECT_TRUE(activity.searchTemplate.empty());
  EXPECT_FALSE(activity.prevRowPresent);
  EXPECT_FALSE(activity.nextRowPresent);
  EXPECT_EQ(activity.nav.selected, 0);
  EXPECT_FALSE(feedTest.staleEntriesAtDownload);
  EXPECT_LT(eventIndex("routing-close"), eventIndex("render-wait"));
  EXPECT_LT(eventIndex("render-wait"), eventIndex("sd-font-release"));
  EXPECT_LT(eventIndex("font-cache-release"), eventIndex("http-start"));
}

TEST_F(OpdsFeedLoading, UsesMemoryRecoveredByFontReleaseBeforePreflight) {
  feedTest.freeHeap = 1000;
  feedTest.heapAfterFontRelease = HttpDownloader::MIN_TLS_FREE_HEAP;
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  EXPECT_EQ(feedTest.downloadCalls, 1);
  expectClean();
}

TEST_F(OpdsFeedLoading, PlainHttpDoesNotRequireTheTlsHeapFloor) {
  activity.server.url = "http://library.local/opds";
  feedTest.freeHeap = HttpDownloader::MIN_TLS_FREE_HEAP - 1;
  feedTest.maxAlloc = HttpDownloader::MIN_TLS_MAX_ALLOC - 1;
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  EXPECT_EQ(feedTest.downloadCalls, 1);
  EXPECT_EQ(feedTest.requestedUrl, "http://library.local/opds");
  expectClean();
}

class OpdsFeedLoadingHeapFloor : public OpdsFeedLoading, public ::testing::WithParamInterface<bool> {};
TEST_P(OpdsFeedLoadingHeapFloor, RejectsInsufficientHeapWithoutStartingHttpOrParser) {
  if (GetParam())
    feedTest.freeHeap = HttpDownloader::MIN_TLS_FREE_HEAP - 1;
  else
    feedTest.maxAlloc = HttpDownloader::MIN_TLS_MAX_ALLOC - 1;
  feedTest.files[feedTempPath] = makeFeed();
  activity.startBrowse();
  expectFailure(STR_MEMORY_ERROR);
  EXPECT_EQ(feedTest.downloadCalls, 0);
  EXPECT_EQ(feedTest.parserCreations, 0);
}
INSTANTIATE_TEST_SUITE_P(HeapLimits, OpdsFeedLoadingHeapFloor, ::testing::Bool());

TEST_F(OpdsFeedLoading, MissingServerUrlDoesNotStartTransfer) {
  activity.server.url.clear();
  activity.startBrowse();
  expectFailure(STR_NO_SERVER_URL);
  EXPECT_EQ(feedTest.downloadCalls, 0);
  EXPECT_EQ(feedTest.parserCreations, 0);
}

class OpdsFeedLoadingDownloadFailure : public OpdsFeedLoading, public ::testing::WithParamInterface<int> {};
TEST_P(OpdsFeedLoadingDownloadFailure, RemovesPartialAndStaleFilesWithoutParsingThem) {
  feedTest.files[feedTempPath] = "stale contents";
  feedTest.downloadResult = GetParam();
  activity.startBrowse();
  expectFailure(STR_FETCH_FEED_FAILED);
  EXPECT_EQ(feedTest.parserCreations, 0);
  EXPECT_LT(eventIndex("file-remove"), eventIndex("http-start"));
  feedTest.downloadResult = HttpDownloader::OK;
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  EXPECT_EQ(activity.entries.size(), 1u);
  expectClean();
}
INSTANTIATE_TEST_SUITE_P(TransferErrors, OpdsFeedLoadingDownloadFailure,
                         ::testing::Values(HttpDownloader::HTTP_ERROR, HttpDownloader::FILE_ERROR,
                                           HttpDownloader::ABORTED, HttpDownloader::UNAUTHORIZED));

TEST_F(OpdsFeedLoading, DirectoryFailureDoesNotStartTransfer) {
  feedTest.directoryOk = false;
  feedTest.files[feedTempPath] = "stale";
  activity.startBrowse();
  expectFailure(STR_FETCH_FEED_FAILED);
  EXPECT_EQ(feedTest.downloadCalls, 0);
  EXPECT_EQ(feedTest.parserCreations, 0);
}

TEST_F(OpdsFeedLoading, CannotRemoveStaleFileFailsClosed) {
  feedTest.removeOk = false;
  feedTest.files[feedTempPath] = makeFeed();
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::ERROR);
  EXPECT_EQ(activity.errorMessage, tr(STR_FETCH_FEED_FAILED));
  EXPECT_EQ(feedTest.downloadCalls, 0);
  EXPECT_EQ(feedTest.parserCreations, 0);
  EXPECT_EQ(feedTest.files.at(feedTempPath), makeFeed());
}

TEST_F(OpdsFeedLoading, OpenFailureRemovesDownloadedFileWithoutCreatingParser) {
  feedTest.openOk = false;
  activity.startBrowse();
  expectFailure(STR_FETCH_FEED_FAILED);
  EXPECT_EQ(feedTest.parserCreations, 0);
}

class OpdsFeedLoadingReadFailure : public OpdsFeedLoading, public ::testing::WithParamInterface<int> {};
TEST_P(OpdsFeedLoadingReadFailure, RejectsReadFailureOrInvalidCountAndReleasesAllResources) {
  feedTest.response = makeFeed(20);
  feedTest.readFailureAt = 2;
  feedTest.readFailureResult = GetParam();
  activity.startBrowse();
  expectFailure(STR_FETCH_FEED_FAILED);
  EXPECT_EQ(feedTest.readCalls, 2);
}
INSTANTIATE_TEST_SUITE_P(ReadErrors, OpdsFeedLoadingReadFailure, ::testing::Values(-1, 0, 1025));

TEST_F(OpdsFeedLoading, HandlesShortReadsUntilAllFileBytesHaveBeenConsumed) {
  feedTest.response = makeFeed(10);
  feedTest.maxRead = 17;
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  EXPECT_EQ(activity.entries.size(), 10u);
  EXPECT_GT(feedTest.readCalls, 10);
  EXPECT_LE(feedTest.largestReadRequest, 1024u);
  expectClean();
}

TEST_F(OpdsFeedLoading, EarlyEofIsAFileErrorEvenIfAvailableXmlIsComplete) {
  feedTest.reportedSize = static_cast<int64_t>(feedTest.response.size() + 100);
  activity.startBrowse();
  expectFailure(STR_FETCH_FEED_FAILED);
}

TEST_F(OpdsFeedLoading, FlushDetectsXmlTruncatedExactlyAtFileEof) {
  feedTest.response.resize(feedTest.response.size() - 7);
  activity.startBrowse();
  expectFailure(STR_PARSE_FEED_FAILED);
  EXPECT_LT(eventIndex("file-close"), eventIndex("parser-flush"));
}

TEST_F(OpdsFeedLoading, MalformedXmlStopsFurtherReadsAndCleansUp) {
  feedTest.response = "<feed><entry></feed>" + std::string(5000, ' ');
  activity.startBrowse();
  expectFailure(STR_PARSE_FEED_FAILED);
  EXPECT_EQ(feedTest.readCalls, 1);
}

TEST_F(OpdsFeedLoading, EmptyFileIsAParseFailure) {
  feedTest.response.clear();
  activity.startBrowse();
  expectFailure(STR_PARSE_FEED_FAILED);
}

TEST_F(OpdsFeedLoading, ValidEmptyFeedUsesNoEntriesMessage) {
  feedTest.response = makeFeed(0);
  activity.startBrowse();
  expectFailure(STR_NO_ENTRIES);
}

TEST_F(OpdsFeedLoading, BufferAllocationFailureUsesMemoryMessageAndCleansFile) {
  feedTest.failBufferAllocation = true;
  activity.startBrowse();
  expectFailure(STR_MEMORY_ERROR);
  EXPECT_EQ(feedTest.parserCreations, 0);
}

TEST_F(OpdsFeedLoading, ParserAllocationFailureUsesMemoryMessageAndCleansFile) {
  feedTest.failParserCreation = true;
  activity.startBrowse();
  expectFailure(STR_MEMORY_ERROR);
  EXPECT_EQ(feedTest.parserCreations, 1);
  EXPECT_EQ(feedTest.readCalls, 0);
}

TEST_F(OpdsFeedLoading, GoodBadGoodFeedSequenceRecoversWithoutRestart) {
  for (int attempt = 0; attempt < 12; ++attempt) {
    feedTest.response = makeFeed();
    activity.startBrowse();
    ASSERT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
    ASSERT_EQ(activity.entries.size(), 1u);
    expectClean();
    feedTest.response = attempt % 2 ? "<feed><entry>" : "<feed><entry></feed>";
    activity.startBrowse();
    expectFailure(STR_PARSE_FEED_FAILED);
    feedTest.response = makeFeed();
    activity.startBrowse();
    ASSERT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
    ASSERT_EQ(activity.entries.size(), 1u);
    expectClean();
  }
}

TEST_F(OpdsFeedLoading, RetryAfterParserAllocationFailureRecovers) {
  feedTest.failParserCreation = true;
  activity.startBrowse();
  expectFailure(STR_MEMORY_ERROR);
  feedTest.failParserCreation = false;
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  EXPECT_EQ(activity.entries.size(), 1u);
  expectClean();
}

TEST_F(OpdsFeedLoading, BoundedEntriesKeepPaginationAndSearchFromEndOfLargeFeed) {
  feedTest.response = makeFeed(80);
  // Feed navigation may occur after every book, beyond the entry retention cap.
  feedTest.response.resize(feedTest.response.size() - 7);
  feedTest.response += R"(<link rel="previous" href="?page=1"/><link rel="next" href="?page=3"/>)";
  feedTest.response += R"(<link rel="search" type="application/atom+xml" href="/search?q={searchTerms}"/></feed>)";
  activity.startBrowse();
  ASSERT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  ASSERT_EQ(activity.entries.size(), 64u);
  EXPECT_TRUE(activity.prevRowPresent);
  EXPECT_TRUE(activity.nextRowPresent);
  EXPECT_EQ(activity.entries.front().href, "?page=1");
  EXPECT_EQ(activity.entries.back().href, "?page=3");
  EXPECT_EQ(activity.entries[1].title, "Book 0");
  EXPECT_EQ(activity.entries[62].title, "Book 61");
  EXPECT_EQ(activity.searchTemplate, "/search?q={searchTerms}");
  EXPECT_EQ(activity.rowItems.size(), activity.entries.size());
  EXPECT_GT(feedTest.yields, 0);
  EXPECT_EQ(feedTest.yields, feedTest.watchdogResets);
  EXPECT_LE(feedTest.largestReadRequest, 1024u);
  expectClean();
}

struct UrlCase {
  const char* name;
  const char* path;
  const char* expected;
};
void PrintTo(const UrlCase& value, std::ostream* output) { *output << value.name; }
class OpdsFeedLoadingUrl : public OpdsFeedLoading, public ::testing::WithParamInterface<UrlCase> {};
TEST_P(OpdsFeedLoadingUrl, PreservesExistingUrlResolution) {
  activity.currentPath = GetParam().path;
  activity.startBrowse();
  EXPECT_EQ(activity.state, OpdsBookBrowserActivity::State::BROWSING);
  EXPECT_EQ(feedTest.requestedUrl, GetParam().expected);
  EXPECT_EQ(feedTest.username, "reader");
  EXPECT_EQ(feedTest.password, "secret");
  expectClean();
}
INSTANTIATE_TEST_SUITE_P(
    Urls, OpdsFeedLoadingUrl,
    ::testing::Values(UrlCase{"Relative", "shelf/Recent books", "https://library.example/opds/shelf/Recent%20books"},
                      UrlCase{"RootRelative", "/shelf?page=2", "https://library.example/shelf?page=2"},
                      UrlCase{"Absolute", "https://other.example/books?page=3", "https://other.example/books?page=3"}),
    [](const ::testing::TestParamInfo<UrlCase>& info) { return info.param.name; });
}  // namespace
