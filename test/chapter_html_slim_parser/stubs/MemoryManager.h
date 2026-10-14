#pragma once

// Host-test stub for the SDK MemoryManager. The parser/layout path calls
// ensureFree() to evict rebuildable caches before large allocations; on the
// host it reports success. Tests can inject heap recovery through clearCaches()
// to exercise the parser's pre-allocation low-memory guard.

#include <cstddef>
#include <functional>

namespace freeink {

enum class MemPool : unsigned char { Internal, Psram, Default };

class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager inst;
    return inst;
  }
  bool ensureFree(size_t, MemPool = MemPool::Default) { return true; }
  size_t clearCaches(size_t bytes = 0) {
    ++clearCachesCalls;
    return onClearCaches ? onClearCaches(bytes) : 0;
  }
  unsigned clearCachesCalls = 0;
  std::function<size_t(size_t)> onClearCaches;

 private:
  MemoryManager() = default;
};

}  // namespace freeink
