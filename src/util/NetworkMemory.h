#pragma once

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "ReadingStatsStore.h"
#include "SdCardFontGlobals.h"
#include "util/TimeUtils.h"

namespace NetworkMemory {

inline void logSnapshot(const char* tag, const char* stage) {
  const uint32_t freeHeap = esp_get_free_heap_size();
  const uint32_t contigHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  LOG_DBG(tag, "Network mem[%s]: free=%lu contig=%lu", stage, freeHeap, contigHeap);
}

inline void trimRendererCaches(const GfxRenderer& renderer, const char* tag) {
  if (auto* cacheManager = renderer.getFontCacheManager()) {
    cacheManager->clearCache();
    cacheManager->resetStats();
    LOG_DBG(tag, "Cleared font cache before network");
  }

  unsigned releasedSdFonts = 0;
  for (const auto& entry : renderer.getSdCardFonts()) {
    if (!entry.second) {
      continue;
    }
    entry.second->releaseForLowMemory();
    releasedSdFonts++;
  }
  if (releasedSdFonts > 0) {
    LOG_DBG(tag, "Released %u SD font runtime cache(s) before network", releasedSdFonts);
  }
}

inline void prepareBeforeNetwork(GfxRenderer& renderer, const char* tag, const char* stage,
                                 const bool releaseReadingStats = true) {
  TimeUtils::stopNtp();
  trimRendererCaches(renderer, tag);

  if (Storage.ready()) {
    sdFontSystem.releaseForNetwork(renderer);
  }

  if (releaseReadingStats && !READING_STATS.releaseMemoryForNetwork()) {
    LOG_ERR(tag, "Failed to release reading stats memory before network");
  }

  delay(20);
  logSnapshot(tag, stage);
}

inline void restoreAfterNetwork(GfxRenderer& renderer, const char* tag, const char* stage,
                                const bool reloadReadingStats = true) {
  if (reloadReadingStats && !READING_STATS.reloadAfterNetwork()) {
    LOG_ERR(tag, "Failed to reload reading stats after network");
  }

  if (Storage.ready()) {
    sdFontSystem.ensureLoaded(renderer);
  }

  delay(10);
  logSnapshot(tag, stage);
}

}  // namespace NetworkMemory
