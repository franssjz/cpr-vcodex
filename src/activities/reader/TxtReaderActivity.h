#pragma once

#include <Txt.h>

#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "util/ChapterTimeEstimate.h"

class TxtReaderActivity final : public Activity {
 public:
  struct TextLine {
    struct TextSpan {
      std::string text;
      uint8_t style = 0;
    };

    std::string text;
    std::vector<TextSpan> spans;
    uint8_t style = 0;
    uint8_t alignment = CrossPointSettings::LEFT_ALIGN;
    uint8_t indent = 0;
  };

 private:
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  // Book-wide word total. Per-page counts live only in the index cache on disk
  // (not in RAM) so large TXT files stay within ESP32-C3 heap limits.
  uint32_t totalBookWords = 0;
  // Byte offset of the per-page word table inside index.bin after a successful load/save.
  uint32_t wordCountsFileOffset = 0;
  // Remaining words from currentPage inclusive; refreshed from disk when invalid.
  uint32_t cachedRemainingWords = 0;
  bool cachedRemainingValid = false;
  std::vector<TextLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;
  bool statusBarTemporarilyHidden = false;
  std::string stableBookId;
  bool pendingForceFullRefresh = false;
  bool waitingForConfirmSecondClick = false;
  unsigned long firstConfirmClickMs = 0UL;
  // Word-rate samples: dwell on the page currently displayed.
  ChapterTimeEstimate::PageDwell pageDwell;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<TextLine>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache(const std::vector<uint16_t>& pageWords);
  bool readCachedPageWordCount(int page, uint16_t& outWords) const;
  bool sumRemainingWordsFromCache(int fromPage, uint32_t& outRemaining) const;
  void invalidateRemainingWordsCache();
  void ensureRemainingWordsCache();
  uint32_t estimateRemainingWords(int fromPage) const;
  void saveProgress() const;
  void loadProgress();
  void requestCurrentPageFullRefresh();
  void toggleTemporaryStatusBar();
  void creditCurrentPageWords();
  void maybeCreditPageWords(int page);
  void resumeAfterSubactivity();
  void openReaderSubactivity(std::unique_ptr<Activity>&& activity, ActivityResultHandler onResult);
  uint32_t countWordsInLines(const std::vector<TextLine>& lines) const;
  std::string moveCompletedBookIfEnabled();
  void exitReaderAfterOptionalCompletedMove();

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt)
      : Activity("TxtReader", renderer, mappedInput), txt(std::move(txt)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
