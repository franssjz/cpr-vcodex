#include <gtest/gtest.h>

#include <limits>
#include <sstream>

#include "StablePageNumbers.h"

namespace {
JsonDocument manifest() {
  JsonDocument doc;
  deserializeJson(doc, R"({"format":"x-locations","version":1,"totalCharacters":6000,
    "charactersPerReferencePage":1500,"totalReferencePages":4,
    "spine":[{"index":0,"characterStart":0,"characterCount":0},
             {"index":1,"characterStart":0,"characterCount":3000},
             {"index":2,"characterStart":3000,"characterCount":3000}]})");
  return doc;
}
void expectPage(const StablePageNumbers& pages, int spine, float progress, uint32_t expected, uint32_t total = 4) {
  uint32_t page = 99, count = 99;
  ASSERT_TRUE(pages.resolve(spine, progress, page, count));
  EXPECT_EQ(page, expected);
  EXPECT_EQ(count, total);
}
}  // namespace

TEST(StablePageNumbers, CharacterBoundariesEmptySpinesAndClamping) {
  StablePageNumbers pages;
  auto doc = manifest();
  ASSERT_TRUE(pages.load(doc.as<JsonVariantConst>(), 3));
  expectPage(pages, 0, 0, 1);
  expectPage(pages, 1, -1, 1);
  expectPage(pages, 1, 0.499f, 1);
  expectPage(pages, 1, 0.5f, 2);
  expectPage(pages, 2, 0, 3);
  expectPage(pages, 2, 0.5f, 4);
  expectPage(pages, 2, 10, 4);
}

TEST(StablePageNumbers, SupportsLegacyWordMetadataAndDerivedTotal) {
  JsonDocument doc;
  deserializeJson(doc, R"({"format":"crossink-locations","version":1,"totalWords":501,
    "wordsPerReferencePage":250,"spine":[{"wordStart":0,"wordCount":501}]})");
  StablePageNumbers pages;
  ASSERT_TRUE(pages.load(doc.as<JsonVariantConst>(), 1));
  expectPage(pages, 0, 0, 1, 3);
  expectPage(pages, 0, 1, 3, 3);
}

TEST(StablePageNumbers, RejectsMismatchedTotalsMissingSpinesAndInvalidUnits) {
  for (const char* key : {"totalCharacters", "charactersPerReferencePage", "totalReferencePages", "version"}) {
    auto doc = manifest();
    doc[key] = -1;
    StablePageNumbers pages;
    EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), 3)) << key;
  }
  StablePageNumbers pages;
  auto doc = manifest();
  EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), 2));
  EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), 0));
  EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), StablePageNumbers::MAX_SPINES + 1));
  doc["charactersPerReferencePage"] = 0;
  EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), 3));
}

TEST(StablePageNumbers, RejectsDuplicateIndicesOverlapsGapsOverflowAndIncompleteRanges) {
  for (const char* key : {"index", "characterStart", "characterCount"}) {
    for (uint32_t value : {0U, 1U, 6001U, UINT32_MAX}) {
      auto doc = manifest();
      doc["spine"][2][key] = value;
      StablePageNumbers pages;
      EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), 3)) << key << "=" << value;
    }
  }
}

TEST(StablePageNumbers, FailedReloadClearsOldBookAndInvalidPositionsDoNotLeakOutput) {
  StablePageNumbers pages;
  auto doc = manifest();
  ASSERT_TRUE(pages.load(doc.as<JsonVariantConst>(), 3));
  uint32_t page = 99, count = 99;
  EXPECT_FALSE(pages.resolve(3, 0, page, count));
  EXPECT_EQ(page, 0U);
  EXPECT_EQ(count, 0U);
  EXPECT_FALSE(pages.resolve(-1, 0, page, count));
  EXPECT_FALSE(pages.resolve(0, std::numeric_limits<float>::quiet_NaN(), page, count));
  doc["format"] = "unsupported";
  EXPECT_FALSE(pages.load(doc.as<JsonVariantConst>(), 3));
  EXPECT_FALSE(pages.resolve(1, 0, page, count));
}

TEST(StablePageNumbers, LargeValidCountsDoNotOverflow) {
  JsonDocument doc;
  deserializeJson(doc, R"({"format":"x-locations","version":1,"totalCharacters":4294967295,
    "charactersPerReferencePage":1500,"spine":[{"characterStart":0,"characterCount":4294967295}]})");
  StablePageNumbers pages;
  ASSERT_TRUE(pages.load(doc.as<JsonVariantConst>(), 1));
  expectPage(pages, 0, 1, 2863312, 2863312);
}

TEST(StablePageNumbers, FilteredStreamSkipsUnrelatedMetadataAndRejectsTruncation) {
  auto source = manifest();
  source["unknown"]["nested"] = std::string(6000, 'x');
  std::string json;
  serializeJson(source, json);
  for (bool truncate : {false, true}) {
    std::istringstream stream(truncate ? json.substr(0, json.size() - 1) : json);
    StablePageJsonAllocator allocator;
    JsonDocument doc(&allocator), filter;
    StablePageNumbers::configureFilter(filter);
    auto error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    EXPECT_EQ(static_cast<bool>(error), truncate);
    if (!error) {
      StablePageNumbers pages;
      EXPECT_TRUE(pages.load(doc.as<JsonVariantConst>(), 3));
      EXPECT_TRUE(doc["unknown"].isNull());
    }
  }
}

TEST(StablePageNumbers, AllocatorBoundsAndReclaimsMemoryIncludingFailedReallocation) {
  StablePageJsonAllocator allocator;
  EXPECT_EQ(allocator.allocate(StablePageNumbers::JSON_BUDGET), nullptr);
  void* p = allocator.allocate(100);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(allocator.reallocate(p, SIZE_MAX), nullptr);
  p = allocator.reallocate(p, 200);
  ASSERT_NE(p, nullptr);
  allocator.deallocate(p);
  p = allocator.allocate(StablePageNumbers::JSON_BUDGET - 64);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(allocator.allocate(100), nullptr);
  allocator.deallocate(p);
}

TEST(StablePageNumbers, PathologicalFilteredArrayFailsWithinBudget) {
  std::string json = "{\"spine\":[";
  for (int i = 0; i < 10000; ++i) json += (i ? ",{}" : "{}");
  json += "]}";
  std::istringstream stream(json);
  StablePageJsonAllocator allocator;
  JsonDocument doc(&allocator), filter;
  StablePageNumbers::configureFilter(filter);
  EXPECT_EQ(deserializeJson(doc, stream, DeserializationOption::Filter(filter)), DeserializationError::NoMemory);
}
