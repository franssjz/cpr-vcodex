#pragma once
#include <cstdint>
namespace MemoryBudget {
struct HeapSnapshot {
  uint32_t freeHeap;
  uint32_t maxAllocHeap;
};
inline HeapSnapshot heap{256 * 1024, 128 * 1024};
inline HeapSnapshot snapshot() { return heap; }
inline bool hasHeap(HeapSnapshot current, uint32_t free, uint32_t largest) {
  return current.freeHeap >= free && current.maxAllocHeap >= largest;
}
}  // namespace MemoryBudget
