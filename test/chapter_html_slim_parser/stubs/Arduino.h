#pragma once

#include <cstddef>
#include <cstdint>

struct EspHostStub {
  uint32_t maxAllocHeap = 1u << 20;
  uint32_t freeHeap = UINT32_MAX;
  uint32_t getMaxAllocHeap() const { return maxAllocHeap; }
  uint32_t getFreeHeap() const { return freeHeap; }
};

inline EspHostStub ESP;
