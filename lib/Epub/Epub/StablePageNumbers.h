#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <memory>

// CrossInk x-locations v1 reference pages. Only the compact spine ranges survive
// loading; resolving a page never allocates or accesses the SD card.
class StablePageNumbers {
 public:
  static constexpr size_t MAX_MANIFEST_BYTES = 64 * 1024;
  static constexpr size_t MAX_SPINES = 1024;
  static constexpr size_t JSON_BUDGET = 48 * 1024;

  void clear();
  static void configureFilter(JsonDocument& filter);
  bool load(JsonVariantConst root, size_t spineCount);
  bool resolve(int spineIndex, float progress, uint32_t& page, uint32_t& count) const;

 private:
  struct Range {
    uint32_t start;
    uint32_t count;
  };
  std::unique_ptr<Range[]> ranges;
  size_t rangeCount = 0;
  uint32_t unitsPerPage = 0;
  uint32_t pageCount = 0;
};

// Limit the filtered JSON DOM even when a small untrusted manifest contains
// thousands of tiny objects. Allocation headers preserve malloc's alignment.
class StablePageJsonAllocator final : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t size) override;
  void deallocate(void* ptr) override;
  void* reallocate(void* ptr, size_t newSize) override;

 private:
  struct alignas(std::max_align_t) Header {
    size_t size;
  };
  size_t used = 0;
};
