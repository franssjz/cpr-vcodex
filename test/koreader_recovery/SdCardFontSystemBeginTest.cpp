#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
bool countAllocations = false;
size_t allocations = 0;
}  // namespace

// Scope counting to the production begin call, excluding GTest's own storage.
void* operator new(size_t size) {
  if (countAllocations) ++allocations;
  if (void* memory = std::malloc(size ? size : 1)) return memory;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }

namespace {
struct GfxRenderer {};
struct {
  int (*sdFontIdResolver)(void*, const char*, uint8_t) = nullptr;
  void* sdFontResolverCtx = nullptr;
  char sdFontFamilyName[64]{};
} settings;
#define SETTINGS settings
#define LOG_DBG(...) ((void)0)

class SdCardFontSystem {
 public:
#include "SdCardFontBeginDecl.inc"
  void ensureLoaded(GfxRenderer&) {
    ++loadCalls;
    fontData = std::make_unique<char[]>(16);
  }
  int resolveFontId(const char* family, uint8_t pointSize) const {
    ++resolveCalls;
    lastPointSize = pointSize;
    return fontData && std::strcmp(family, SETTINGS.sdFontFamilyName) == 0 ? 1234 : 0;
  }
  unsigned loadCalls = 0;
  mutable unsigned resolveCalls = 0;
  mutable uint8_t lastPointSize = 0;

 private:
  std::unique_ptr<char[]> fontData;
};
#include "SdCardFontBegin.inc"

class SdCardFontSystemBegin : public testing::Test {
 protected:
  GfxRenderer renderer;
  SdCardFontSystem system;
  void SetUp() override {
    settings = {};
    std::strcpy(settings.sdFontFamilyName, "Selected SD Font");
    allocations = 0;
    countAllocations = false;
  }
  void TearDown() override { countAllocations = false; }
};

TEST_F(SdCardFontSystemBegin, NetworkBootInstallsResolverWithoutAllocatingOrLoadingSelectedFont) {
  countAllocations = true;
  system.begin(renderer, false);
  countAllocations = false;
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(system.loadCalls, 0u);
  ASSERT_NE(settings.sdFontIdResolver, nullptr);
  EXPECT_EQ(settings.sdFontResolverCtx, &system);
  EXPECT_EQ(settings.sdFontIdResolver(settings.sdFontResolverCtx, settings.sdFontFamilyName, 18), 0);
  EXPECT_EQ(system.resolveCalls, 1u);
  EXPECT_EQ(system.lastPointSize, 18);
  // NetworkMemory::restoreAfterNetwork uses ensureLoaded without another begin.
  system.ensureLoaded(renderer);
  EXPECT_EQ(settings.sdFontIdResolver(settings.sdFontResolverCtx, settings.sdFontFamilyName, 18), 1234);
  EXPECT_EQ(system.loadCalls, 1u);
}

TEST_F(SdCardFontSystemBegin, DefaultBeginStillLoadsSelectedFontAndRegistersResolver) {
  system.begin(renderer);
  EXPECT_EQ(system.loadCalls, 1u);
  ASSERT_NE(settings.sdFontIdResolver, nullptr);
  EXPECT_EQ(settings.sdFontResolverCtx, &system);
  EXPECT_EQ(settings.sdFontIdResolver(settings.sdFontResolverCtx, settings.sdFontFamilyName, 14), 1234);
  EXPECT_EQ(system.lastPointSize, 14);
}

TEST_F(SdCardFontSystemBegin, BuiltinSelectionStillDefersDiscoveryAndInstallsResolver) {
  settings.sdFontFamilyName[0] = '\0';
  countAllocations = true;
  system.begin(renderer);
  countAllocations = false;
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(system.loadCalls, 0u);
  EXPECT_EQ(settings.sdFontResolverCtx, &system);
  EXPECT_NE(settings.sdFontIdResolver, nullptr);
}

TEST_F(SdCardFontSystemBegin, DeferredBeginRebindsAnExistingResolverToTheCurrentSystem) {
  SdCardFontSystem previous;
  previous.begin(renderer);
  system.begin(renderer, false);
  EXPECT_EQ(settings.sdFontResolverCtx, &system);
  EXPECT_EQ(system.loadCalls, 0u);
  ASSERT_NE(settings.sdFontIdResolver, nullptr);
  settings.sdFontIdResolver(settings.sdFontResolverCtx, settings.sdFontFamilyName, 16);
  EXPECT_EQ(system.resolveCalls, 1u);
  EXPECT_EQ(previous.resolveCalls, 0u);
}
}  // namespace
