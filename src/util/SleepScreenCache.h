#pragma once

#include <cstdint>
#include <string>

class GfxRenderer;

class SleepScreenCache {
 public:
  static bool load(GfxRenderer& renderer, const std::string& sourcePath);
  static void save(const GfxRenderer& renderer, const std::string& sourcePath);
  static int invalidateAll();

 private:
  static constexpr const char* CACHE_DIR = "/.crosspoint/sleep_cache";

  static uint32_t hashKey(const std::string& sourcePath, uint32_t fileSize);
  static std::string getCachePath(const std::string& sourcePath, uint32_t fileSize);
};
