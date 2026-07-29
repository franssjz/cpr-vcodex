#include "EbookImageDarkMode.h"

namespace {

constexpr size_t kMaxTracked = 24;
uint32_t gSeen[kMaxTracked];
size_t gSeenCount = 0;

uint32_t hashPath(const char* path) {
  uint32_t hash = 2166136261u;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(path); *p; ++p) {
    hash ^= *p;
    hash *= 16777619u;
  }
  return hash;
}

// Record path; return true if it was already seen this session.
bool alreadySeen(const char* path) {
  if (!path || !path[0]) {
    return false;
  }
  const uint32_t hash = hashPath(path);
  for (size_t i = 0; i < gSeenCount; ++i) {
    if (gSeen[i] == hash) {
      return true;
    }
  }
  if (gSeenCount < kMaxTracked) {
    gSeen[gSeenCount++] = hash;
  }
  return false;
}

}  // namespace

void ebookImageDarkModeResetSession() { gSeenCount = 0; }

bool ebookImageShouldInvert(GfxRenderer& renderer, const int16_t width, const int16_t height,
                            const char* identityPath) {
  if (!renderer.isDarkMode() || !renderer.isDarkModeInvertImages() || width <= 0 || height <= 0) {
    return false;
  }

  const int screenH = renderer.getScreenHeight();
  // Must be a short page band and at least landscape.
  if (screenH <= 0 || height * 100 > screenH * 15 || width < height * 2) {
    return false;
  }

  // Classic divider (very wide), or same short asset seen before.
  return width >= height * 4 || alreadySeen(identityPath);
}
