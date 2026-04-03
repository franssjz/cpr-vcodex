
#pragma once

#include <Logging.h>
#include <esp_heap_caps.h>

#include <cstdint>

/**
 * @brief Memory monitoring utilities for debugging heap fragmentation on RAM-constrained ESP32-C3
 *
 * Used to track heap state and diagnose memory issues during development.
 * Since ESP32-C3 has only ~380 KB usable RAM with no PSRAM, heap fragmentation can
 * cause allocation failures despite apparent free space.
 *
 * @note These utilities should be called sparingly in production to avoid log spam
 */
namespace MemoryMonitor {

/**
 * @brief Snapshot of heap statistics
 */
struct HeapStats {
  uint32_t freeHeap;      /**< Total free heap in bytes */
  uint32_t minFreeHeap;   /**< Minimum free heap since boot */
  uint32_t largestBlock;  /**< Size of largest contiguous block available */
  uint32_t allocatedHeap; /**< Total allocated heap in bytes */

  /**
   * @brief Estimated fragmentation ratio (0.0-1.0)
   * High ratio (>0.7) indicates significant fragmentation
   */
  float fragmentation() const {
    if (freeHeap == 0) return 0.0f;
    return 1.0f - (static_cast<float>(largestBlock) / static_cast<float>(freeHeap));
  }
};

/**
 * @brief Capture current heap statistics
 * @return HeapStats snapshot of current memory state
 * @note O(1) operation, safe to call frequently for monitoring
 */
inline HeapStats captureHeap() {
  return {.freeHeap = ESP.getFreeHeap(),
          .minFreeHeap = ESP.getMinFreeHeap(),
          .largestBlock = ESP.getMaxAllocHeap(),
          .allocatedHeap = heap_caps_get_allocated_size(MALLOC_CAP_DEFAULT)};
}

/**
 * @brief Log current heap state with custom label
 * @param label Snapshot identifier (e.g., "After EPUB load", "Before boot")
 *
 * Output format:
 *   MEM | [label]: Free=XXXkB, Min=XXXkB, MaxBlock=XXXkB, Frag=X.X%
 *
 * @note Should be used during development, not in production due to log overhead
 */
inline void logHeap(const char* label) {
  const auto stats = captureHeap();
  const float fragPct = stats.fragmentation() * 100.0f;
  LOG_DBG("MEM", "%s: Free=%uB, Min=%uB, MaxBlock=%uB, Frag=%.1f%%", label, stats.freeHeap, stats.minFreeHeap,
          stats.largestBlock, fragPct);
}

/**
 * @brief Check if heap is dangerously fragmented
 * @param maxAcceptableFrag Fragmentation ratio to consider unsafe (default 0.7 = 70%)
 * @return true if fragmentation exceeds threshold
 *
 * Useful for detecting when memory allocation patterns are creating problematic fragmentation
 */
inline bool isFragmented(float maxAcceptableFrag = 0.7f) {
  const auto stats = captureHeap();
  return stats.fragmentation() > maxAcceptableFrag;
}

/**
 * @brief Log heap stats if fragmentation exceeds threshold
 * @param label Snapshot label
 * @param threshold Fragmentation threshold (default 0.6 = 60%)
 *
 * Conditional logging to reduce spam while catching fragmentation issues
 */
inline void logIfFragmented(const char* label, float threshold = 0.6f) {
  if (isFragmented(threshold)) {
    logHeap(label);
  }
}

/**
 * @brief Alert if heap is critically low
 * @param criticalThreshold Minimum free bytes to consider critical (default 50 KB)
 * @param label Optional alert label
 * @return true if heap below critical threshold
 */
inline bool checkCriticalHeap(uint32_t criticalThreshold = 50000, const char* label = nullptr) {
  const auto stats = captureHeap();
  if (stats.freeHeap < criticalThreshold) {
    if (label) {
      LOG_ERR("MEM", "CRITICAL LOW: %s, Free=%uB (below %uB threshold)", label, stats.freeHeap, criticalThreshold);
    } else {
      LOG_ERR("MEM", "CRITICAL LOW: Free=%uB (below %uB threshold)", stats.freeHeap, criticalThreshold);
    }
    return true;
  }
  return false;
}

}  // namespace MemoryMonitor
