#include "StablePageNumbers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

void* StablePageJsonAllocator::allocate(const size_t size) {
  if (size > StablePageNumbers::JSON_BUDGET - used || sizeof(Header) > StablePageNumbers::JSON_BUDGET - used - size)
    return nullptr;
  auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!header) return nullptr;
  header->size = size;
  used += sizeof(Header) + size;
  return header + 1;
}

void StablePageJsonAllocator::deallocate(void* ptr) {
  if (!ptr) return;
  auto* header = static_cast<Header*>(ptr) - 1;
  used -= sizeof(Header) + header->size;
  std::free(header);
}

void* StablePageJsonAllocator::reallocate(void* ptr, const size_t newSize) {
  if (!ptr) return allocate(newSize);
  auto* header = static_cast<Header*>(ptr) - 1;
  const size_t oldSize = header->size;
  if (newSize > oldSize && newSize - oldSize > StablePageNumbers::JSON_BUDGET - used) return nullptr;
  auto* resized = static_cast<Header*>(std::realloc(header, sizeof(Header) + newSize));
  if (!resized) return nullptr;
  used = used - oldSize + newSize;
  resized->size = newSize;
  return resized + 1;
}

void StablePageNumbers::clear() {
  ranges.reset();
  rangeCount = 0;
  unitsPerPage = pageCount = 0;
}

void StablePageNumbers::configureFilter(JsonDocument& filter) {
  for (const char* key : {"format", "version", "totalWords", "totalCharacters", "wordsPerReferencePage",
                          "charactersPerReferencePage", "totalReferencePages"})
    filter[key] = true;
  for (const char* key : {"index", "wordStart", "wordCount", "characterStart", "characterCount"})
    filter["spine"][0][key] = true;
}

bool StablePageNumbers::load(const JsonVariantConst root, const size_t spineCount) {
  clear();
  const char* format = root["format"] | "";
  if ((std::strcmp(format, "x-locations") != 0 && std::strcmp(format, "crossink-locations") != 0) ||
      !root["version"].is<unsigned>() || root["version"].as<unsigned>() != 1 || spineCount == 0 ||
      spineCount > MAX_SPINES)
    return false;
  const bool characters = root["totalCharacters"].is<uint32_t>() && root["totalCharacters"].as<uint32_t>() > 0 &&
                          root["charactersPerReferencePage"].is<uint32_t>() &&
                          root["charactersPerReferencePage"].as<uint32_t>() > 0;
  const auto totalValue = root[characters ? "totalCharacters" : "totalWords"];
  const auto perPageValue = root[characters ? "charactersPerReferencePage" : "wordsPerReferencePage"];
  if (!totalValue.is<uint32_t>() || !perPageValue.is<uint32_t>()) return false;
  const uint32_t total = totalValue.as<uint32_t>();
  const uint32_t perPage = perPageValue.as<uint32_t>();
  if (!total || !perPage) return false;
  const uint32_t pages = 1 + (total - 1) / perPage;
  if (pages > static_cast<uint32_t>(std::numeric_limits<int>::max())) return false;
  if (!root["totalReferencePages"].isNull() &&
      (!root["totalReferencePages"].is<uint32_t>() || root["totalReferencePages"].as<uint32_t>() != pages))
    return false;
  const auto spine = root["spine"].as<JsonArrayConst>();
  if (spine.isNull() || spine.size() != spineCount) return false;
  auto parsed = std::unique_ptr<Range[]>(new (std::nothrow) Range[spineCount]);
  if (!parsed) return false;
  uint32_t end = 0;
  size_t index = 0;
  for (const auto item : spine) {
    if (!item.is<JsonObjectConst>() ||
        (!item["index"].isNull() && (!item["index"].is<size_t>() || item["index"].as<size_t>() != index)))
      return false;
    const auto startValue = item[characters ? "characterStart" : "wordStart"];
    const auto countValue = item[characters ? "characterCount" : "wordCount"];
    if (!startValue.is<uint32_t>() || !countValue.is<uint32_t>()) return false;
    const uint32_t start = startValue.as<uint32_t>();
    const uint32_t count = countValue.as<uint32_t>();
    if (start != end || start > total || count > total - start) return false;
    parsed[index++] = {start, count};
    end = start + count;
  }
  if (end != total) return false;
  ranges = std::move(parsed);
  rangeCount = spineCount;
  unitsPerPage = perPage;
  pageCount = pages;
  return true;
}

bool StablePageNumbers::resolve(const int spineIndex, const float progress, uint32_t& page, uint32_t& count) const {
  page = count = 0;
  if (!ranges || spineIndex < 0 || static_cast<size_t>(spineIndex) >= rangeCount || !std::isfinite(progress))
    return false;
  const auto& range = ranges[spineIndex];
  const double completed = range.start + static_cast<double>(std::clamp(progress, 0.0f, 1.0f)) * range.count;
  page = static_cast<uint32_t>(std::min<double>(std::floor(completed / unitsPerPage) + 1, pageCount));
  count = pageCount;
  return true;
}
