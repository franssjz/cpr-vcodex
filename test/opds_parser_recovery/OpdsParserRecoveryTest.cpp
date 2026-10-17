#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>

#include "OpdsParser.h"

namespace {
struct alignas(std::max_align_t) AllocationHeader {
  size_t bytes;
};

size_t liveBytes = 0;
size_t liveBlocks = 0;
size_t allocationAttempts = 0;
size_t failedAllocation = 0;
bool failureInjected = false;

bool shouldFailAllocation() {
  ++allocationAttempts;
  if (failedAllocation != 0 && allocationAttempts == failedAllocation) {
    failureInjected = true;
    return true;
  }
  return false;
}

void* trackedMalloc(const size_t size) {
  if (shouldFailAllocation()) return nullptr;
  auto* header = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + size));
  if (!header) return nullptr;
  header->bytes = size;
  liveBytes += size;
  ++liveBlocks;
  return header + 1;
}

void trackedFree(void* ptr) {
  if (!ptr) return;
  auto* header = static_cast<AllocationHeader*>(ptr) - 1;
  liveBytes -= header->bytes;
  --liveBlocks;
  std::free(header);
}

void* trackedRealloc(void* ptr, const size_t size) {
  if (!ptr) return trackedMalloc(size);
  if (size == 0) {
    trackedFree(ptr);
    return nullptr;
  }
  if (shouldFailAllocation()) return nullptr;
  auto* previous = static_cast<AllocationHeader*>(ptr) - 1;
  const size_t previousSize = previous->bytes;
  auto* next = static_cast<AllocationHeader*>(std::realloc(previous, sizeof(AllocationHeader) + size));
  if (!next) return nullptr;
  next->bytes = size;
  liveBytes = liveBytes - previousSize + size;
  return next + 1;
}

std::string makeFeed(const size_t count) {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\" xmlns:dc=\"http://purl.org/dc/terms/\" "
      "xmlns:opds=\"http://opds-spec.org/2010/catalog\" "
      "xmlns:opensearch=\"http://a9.com/-/spec/opensearch/1.1/\">"
      "<id>urn:booklore:shelf:1</id><title>Example shelf</title>"
      "<updated>2026-10-17T00:00:00Z</updated>"
      "<link rel=\"next\" href=\"/api/v1/opds/catalog?shelfId=1&amp;page=2&amp;size=50\" "
      "type=\"application/atom+xml;profile=opds-catalog;kind=acquisition\"/>"
      "<opensearch:totalResults>100</opensearch:totalResults>";
  for (size_t i = 0; i < count; ++i) {
    xml += "<entry><title>Example café &amp; 한글 &lt;book&gt;</title><id>urn:booklore:book:";
    xml += std::to_string(i);
    xml +=
        "</id><updated>2026-10-17T00:00:00Z</updated>"
        "<author><name>Example Author</name></author>"
        "<dc:publisher>Example Publisher</dc:publisher><dc:language>en</dc:language>"
        "<category term=\"Fiction &amp; examples\"/><summary>";
    for (int paragraph = 0; paragraph < 8; ++paragraph) {
      xml += "&lt;p&gt;Example &amp; summary with &amp;nbsp; literal HTML entity.&lt;/p&gt; ";
    }
    xml +=
        "</summary><dc:identifier>urn:isbn:1234567890</dc:identifier>"
        "<meta property=\"belongs-to-collection\" id=\"series\">Example Series</meta>"
        "<meta property=\"group-position\" refines=\"#series\">1.5</meta>"
        "<link href=\"/api/v1/opds/1/download?fileId=11\" "
        "rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" title=\"EPUB\"/>"
        "<link rel=\"http://opds-spec.org/image\" href=\"/api/v1/opds/1/cover\" type=\"image/jpeg\"/>"
        "</entry>";
  }
  xml += "</feed>";
  return xml;
}

struct ParseOutcome {
  bool error = false;
  bool memoryError = false;
  bool errorBeforeFlush = false;
  bool injected = false;
  bool truncated = false;
  size_t allocations = 0;
  size_t allocationsBeforeFlush = 0;
  size_t entries = 0;
  std::string firstTitle;
  std::string nextUrl;
};

ParseOutcome parseFeed(const std::string& xml, const size_t chunk = 1024, const size_t failAt = 0) {
  EXPECT_EQ(liveBytes, 0u);
  EXPECT_EQ(liveBlocks, 0u);
  allocationAttempts = 0;
  failedAllocation = failAt;
  failureInjected = false;
  ParseOutcome result;
  {
    OpdsParser parser;
    for (size_t pos = 0; pos < xml.size(); pos += chunk) {
      const size_t count = std::min(chunk, xml.size() - pos);
      EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + pos), count), count);
    }
    result.errorBeforeFlush = parser.error();
    result.allocationsBeforeFlush = allocationAttempts;
    parser.flush();
    result.error = parser.error();
    result.memoryError = parser.memoryError();
    result.injected = failureInjected;
    result.truncated = parser.truncated();
    result.allocations = allocationAttempts;
    result.entries = parser.getEntries().size();
    if (!parser.getEntries().empty()) result.firstTitle = parser.getEntries().front().title;
    result.nextUrl = parser.getNextPageUrl();
  }
  EXPECT_EQ(liveBytes, 0u) << "Expat memory retained after parser destruction";
  EXPECT_EQ(liveBlocks, 0u) << "Expat blocks retained after parser destruction";
  return result;
}

class OpdsParserRecovery : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(liveBytes, 0u);
    ASSERT_EQ(liveBlocks, 0u);
    allocationAttempts = failedAllocation = 0;
    failureInjected = false;
  }

  void TearDown() override {
    EXPECT_EQ(liveBytes, 0u);
    EXPECT_EQ(liveBlocks, 0u);
    failedAllocation = 0;
  }
};
}  // namespace

XML_Parser opdsTestParserCreate(const XML_Char* encoding) {
  const XML_Memory_Handling_Suite memory{trackedMalloc, trackedRealloc, trackedFree};
  return XML_ParserCreate_MM(encoding, &memory, nullptr);
}

TEST_F(OpdsParserRecovery, ValidFeedsPreserveTextAndPaginationAcrossChunks) {
  for (const size_t chunk : {1u, 7u, 1024u}) {
    SCOPED_TRACE(chunk);
    const auto result = parseFeed(makeFeed(50), chunk);
    EXPECT_FALSE(result.error);
    EXPECT_FALSE(result.memoryError);
    EXPECT_FALSE(result.truncated);
    EXPECT_EQ(result.entries, 50u);
    EXPECT_EQ(result.firstTitle, "Example café & 한글 <book>");
    EXPECT_EQ(result.nextUrl, "/api/v1/opds/catalog?shelfId=1&page=2&size=50");
  }
}

TEST_F(OpdsParserRecovery, EntryLimitIsNotAParseOrMemoryFailure) {
  const auto result = parseFeed(makeFeed(100));
  EXPECT_FALSE(result.error);
  EXPECT_FALSE(result.memoryError);
  EXPECT_TRUE(result.truncated);
  EXPECT_EQ(result.entries, 62u);
  EXPECT_EQ(result.nextUrl, "/api/v1/opds/catalog?shelfId=1&page=2&size=50");
}

TEST_F(OpdsParserRecovery, MalformedFeedsRemainNonMemoryErrorsAndFreshParsersRecover) {
  for (const auto* invalid : {"<feed><title>bad & value</title></feed>", "<feed><title>&nbsp;</title></feed>",
                              "<feed><entry></feed>", "<feed/>unexpected"}) {
    SCOPED_TRACE(invalid);
    for (int repeat = 0; repeat < 10; ++repeat) {
      const auto failed = parseFeed(invalid, 7);
      EXPECT_TRUE(failed.error);
      EXPECT_FALSE(failed.memoryError);
      const auto recovered = parseFeed(makeFeed(1));
      ASSERT_FALSE(recovered.error);
      EXPECT_FALSE(recovered.memoryError);
      EXPECT_EQ(recovered.entries, 1u);
    }
  }
}

TEST_F(OpdsParserRecovery, IncompleteDocumentReportsNonMemoryErrorAtFinalization) {
  const auto result = parseFeed("<feed><entry><title>incomplete");
  EXPECT_FALSE(result.errorBeforeFlush);
  EXPECT_TRUE(result.error);
  EXPECT_FALSE(result.memoryError);
  EXPECT_FALSE(parseFeed(makeFeed(1)).error);
}

TEST_F(OpdsParserRecovery, WritingAfterFinalizationDoesNotBecomeAMemoryError) {
  OpdsParser parser;
  const std::string xml = "<feed/>";
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  parser.flush();
  ASSERT_FALSE(parser.error());
  EXPECT_EQ(parser.write(static_cast<uint8_t>(' ')), 1u);
  EXPECT_TRUE(parser.error());
  EXPECT_FALSE(parser.memoryError());
}

TEST_F(OpdsParserRecovery, EveryExpatAllocationFailureIsClassifiedAndReleased) {
  const auto feed = makeFeed(50);
  const auto baseline = parseFeed(feed);
  ASSERT_FALSE(baseline.error);
  ASSERT_GT(baseline.allocations, 5u);
  for (size_t failAt = 1; failAt <= baseline.allocations; ++failAt) {
    SCOPED_TRACE(failAt);
    const auto failed = parseFeed(feed, 1024, failAt);
    ASSERT_TRUE(failed.injected);
    EXPECT_TRUE(failed.error);
    EXPECT_TRUE(failed.memoryError);
    const auto recovered = parseFeed(makeFeed(1));
    ASSERT_FALSE(recovered.error);
    EXPECT_FALSE(recovered.memoryError);
    EXPECT_EQ(recovered.entries, 1u);
  }
  RecordProperty("injected_allocation_failures", static_cast<int>(baseline.allocations));
}

TEST_F(OpdsParserRecovery, DeferredAllocationFailureAtFinalizationIsMemoryError) {
  const std::string feed = "<feed><entry ignored=\"" + std::string(8192, 'x') +
                           "\"><title>Deferred</title><link href=\"/books\" "
                           "type=\"application/atom+xml\"/></entry></feed>";
  const auto baseline = parseFeed(feed, 256);
  ASSERT_FALSE(baseline.error);
  ASSERT_GT(baseline.allocations, baseline.allocationsBeforeFlush);
  const auto failed = parseFeed(feed, 256, baseline.allocationsBeforeFlush + 1);
  ASSERT_TRUE(failed.injected);
  EXPECT_FALSE(failed.errorBeforeFlush);
  EXPECT_TRUE(failed.error);
  EXPECT_TRUE(failed.memoryError);
  EXPECT_FALSE(parseFeed(makeFeed(1)).error);
}
