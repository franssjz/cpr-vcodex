#pragma once
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Epub.h"

class Page;
class GfxRenderer;

class Section {
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;

  // Cached page-offset look-up table — loaded once on first loadPageFromSectionFile() call.
  // Avoids re-reading the LUT from SD on every page turn. Size: pageCount × 4 bytes (~200 bytes for 50 pages).
  std::vector<uint32_t> pageLut;

  void writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                              uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                              bool embeddedStyle, uint8_t imageRendering);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

  // Load the page-offset LUT from the section file into memory.
  bool ensureLutLoaded();

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
      : epub(epub),
        spineIndex(spineIndex),
        renderer(renderer),
        filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}
  ~Section() = default;

  // Update the section file path to include viewport dimensions.
  // Enables per-orientation caching so that rotating the device doesn't invalidate the
  // other orientation's cache — the user can switch back without a full rebuild.
  void setViewportDimensions(uint16_t viewportWidth, uint16_t viewportHeight) {
    char buf[16];
    snprintf(buf, sizeof(buf), "_%ux%u", static_cast<unsigned int>(viewportWidth),
             static_cast<unsigned int>(viewportHeight));
    filePath = epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + buf + ".bin";
    pageLut.clear();  // New file path, invalidate any cached LUT
  }
  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                       uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                       uint8_t imageRendering);
  bool clearCache();
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                         uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                         uint8_t imageRendering, const std::function<void()>& popupFn = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();

  // Look up the page number for an anchor id from the section cache file.
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;
};
