#include "TxtReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "AchievementsStore.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReadingStatsStore.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookIdentity.h"
#include "util/ChapterTimeEstimate.h"
#include "util/CompletedBookMover.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
// v9: totalBookWords only (pro-rata remaining / average page credits; no per-page word table).
constexpr uint8_t CACHE_VERSION = 9;
constexpr uint8_t MARKDOWN_QUOTE_INDENT = 1;
constexpr uint8_t MARKDOWN_LIST_INDENT = 1;

std::string getStableProgressPath(const std::string& bookId) {
  return BookIdentity::getStableDataFilePath(bookId, "txt_progress.bin");
}

std::string getLegacyProgressPath(Txt& txt) { return txt.getCachePath() + "/progress.bin"; }

void exitReaderToHomeOrStats(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& bookPath) {
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  showPendingAchievementPopups(renderer);
  const bool countedSession =
      READING_STATS.getLastSessionSnapshot().valid && READING_STATS.getLastSessionSnapshot().counted &&
      READING_STATS.getLastSessionSnapshot().path == bookPath;

  if (SETTINGS.showStatsAfterReading && countedSession && !bookPath.empty()) {
    activityManager.replaceActivity(
        std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, bookPath, ReadingStatsDetailContext{true}));
  } else {
    activityManager.goHome();
  }
}

bool startsWithAt(const std::string& text, const size_t pos, const char* marker) {
  const size_t markerLen = strlen(marker);
  return pos + markerLen <= text.length() && text.compare(pos, markerLen, marker) == 0;
}

std::string trimMarkdownWhitespace(const std::string& text) {
  size_t begin = 0;
  while (begin < text.length() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    begin++;
  }
  size_t end = text.length();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    end--;
  }
  return text.substr(begin, end - begin);
}

EpdFontFamily::Style combineMarkdownStyle(const EpdFontFamily::Style baseStyle, const bool bold, const bool italic) {
  uint8_t style = static_cast<uint8_t>(baseStyle);
  if (bold) {
    style |= EpdFontFamily::BOLD;
  }
  if (italic) {
    style |= EpdFontFamily::ITALIC;
  }
  return static_cast<EpdFontFamily::Style>(style & EpdFontFamily::BOLD_ITALIC);
}

void appendMarkdownSpan(TxtReaderActivity::TextLine& line, const std::string& text, const EpdFontFamily::Style style) {
  if (text.empty()) {
    return;
  }
  line.text += text;
  const uint8_t rawStyle = static_cast<uint8_t>(style);
  if (!line.spans.empty() && line.spans.back().style == rawStyle) {
    line.spans.back().text += text;
    return;
  }
  line.spans.push_back({text, rawStyle});
}

void parseMarkdownInlineSpans(TxtReaderActivity::TextLine& line, const std::string& text,
                              const EpdFontFamily::Style baseStyle) {
  std::string buffer;
  bool bold = false;
  bool italic = false;
  bool code = false;

  const auto flush = [&]() {
    appendMarkdownSpan(line, buffer, code ? baseStyle : combineMarkdownStyle(baseStyle, bold, italic));
    buffer.clear();
  };

  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '\\' && i + 1 < text.length()) {
      buffer += text[++i];
      continue;
    }

    if (c == '`') {
      flush();
      code = !code;
      continue;
    }

    if (!code && c == '!' && i + 1 < text.length() && text[i + 1] == '[') {
      i++;
      continue;
    }

    if (!code && text[i] == '[') {
      const size_t labelEnd = text.find(']', i + 1);
      if (labelEnd != std::string::npos && labelEnd + 1 < text.length() && text[labelEnd + 1] == '(') {
        const size_t urlEnd = text.find(')', labelEnd + 2);
        if (urlEnd != std::string::npos) {
          flush();
          parseMarkdownInlineSpans(line, text.substr(i + 1, labelEnd - i - 1),
                                   combineMarkdownStyle(baseStyle, bold, italic));
          i = urlEnd;
          continue;
        }
      }
    }

    if (!code && (c == '*' || c == '_')) {
      const bool triple = i + 2 < text.length() && text[i + 1] == c && text[i + 2] == c;
      const bool doubleMarker = i + 1 < text.length() && text[i + 1] == c;
      if (triple || doubleMarker) {
        flush();
        if (triple) {
          bold = !bold;
          italic = !italic;
          i += 2;
        } else {
          bold = !bold;
          i++;
        }
        continue;
      }
      if (text.find(c, i + 1) != std::string::npos) {
        flush();
        italic = !italic;
        continue;
      }
    }

    buffer += c;
  }

  flush();
}

TxtReaderActivity::TextLine makePlainTextLine(const std::string& text,
                                              const uint8_t alignment = CrossPointSettings::LEFT_ALIGN) {
  TxtReaderActivity::TextLine line;
  line.text = text;
  line.alignment = alignment;
  if (!text.empty()) {
    line.spans.push_back({text, line.style});
  }
  return line;
}

TxtReaderActivity::TextLine parseMarkdownLine(const std::string& rawLine,
                                              const uint8_t fallbackAlignment = CrossPointSettings::LEFT_ALIGN) {
  TxtReaderActivity::TextLine line;
  line.alignment = fallbackAlignment;

  std::string text = rawLine;
  size_t pos = 0;
  while (pos < text.length() && pos < 3 && text[pos] == ' ') {
    pos++;
  }
  text = text.substr(pos);

  if (text.empty()) {
    return line;
  }

  int headerLevel = 0;
  while (headerLevel < 6 && headerLevel < static_cast<int>(text.length()) && text[headerLevel] == '#') {
    headerLevel++;
  }
  if (headerLevel > 0 && headerLevel < static_cast<int>(text.length()) &&
      std::isspace(static_cast<unsigned char>(text[headerLevel]))) {
    line.style = EpdFontFamily::BOLD;
    line.alignment = CrossPointSettings::CENTER_ALIGN;
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(headerLevel + 1)), EpdFontFamily::BOLD);
    return line;
  }

  if (startsWithAt(text, 0, ">")) {
    size_t quotePos = 1;
    if (quotePos < text.length() && text[quotePos] == ' ') {
      quotePos++;
    }
    line.style = EpdFontFamily::ITALIC;
    line.indent = MARKDOWN_QUOTE_INDENT;
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(quotePos)), EpdFontFamily::ITALIC);
    return line;
  }

  if ((startsWithAt(text, 0, "- ") || startsWithAt(text, 0, "* ") || startsWithAt(text, 0, "+ "))) {
    line.indent = MARKDOWN_LIST_INDENT;
    appendMarkdownSpan(line, "- ", EpdFontFamily::REGULAR);
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(2)), EpdFontFamily::REGULAR);
    return line;
  }

  size_t numberPos = 0;
  while (numberPos < text.length() && std::isdigit(static_cast<unsigned char>(text[numberPos]))) {
    numberPos++;
  }
  if (numberPos > 0 && numberPos + 1 < text.length() && text[numberPos] == '.' && text[numberPos + 1] == ' ') {
    line.indent = MARKDOWN_LIST_INDENT;
    appendMarkdownSpan(line, text.substr(0, numberPos + 2), EpdFontFamily::REGULAR);
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(numberPos + 2)), EpdFontFamily::REGULAR);
    return line;
  }

  if ((startsWithAt(text, 0, "```") || startsWithAt(text, 0, "~~~"))) {
    line.text.clear();
    return line;
  }

  parseMarkdownInlineSpans(line, text, EpdFontFamily::REGULAR);
  return line;
}

int getTextLineWidth(GfxRenderer& renderer, const int fontId, const TxtReaderActivity::TextLine& line) {
  if (line.spans.empty()) {
    return renderer.getTextAdvanceX(fontId, line.text.c_str(), static_cast<EpdFontFamily::Style>(line.style));
  }
  int width = 0;
  for (const auto& span : line.spans) {
    width += renderer.getTextAdvanceX(fontId, span.text.c_str(), static_cast<EpdFontFamily::Style>(span.style));
  }
  return width;
}

TxtReaderActivity::TextLine sliceTextLine(const TxtReaderActivity::TextLine& source, const size_t begin,
                                          const size_t length) {
  TxtReaderActivity::TextLine out;
  out.style = source.style;
  out.alignment = source.alignment;
  out.indent = source.indent;

  const size_t end = begin + length;
  size_t spanBegin = 0;
  for (const auto& span : source.spans) {
    const size_t spanEnd = spanBegin + span.text.length();
    if (spanEnd > begin && spanBegin < end) {
      const size_t localBegin = begin > spanBegin ? begin - spanBegin : 0;
      const size_t localEnd = std::min(span.text.length(), end - spanBegin);
      std::string part = span.text.substr(localBegin, localEnd - localBegin);
      out.text += part;
      out.spans.push_back({std::move(part), span.style});
    }
    spanBegin = spanEnd;
  }

  if (out.spans.empty() && !source.text.empty()) {
    out.text = source.text.substr(begin, length);
    out.spans.push_back({out.text, source.style});
  }

  return out;
}
}  // namespace

void TxtReaderActivity::onEnter() {
  Activity::onEnter();

  if (!txt) {
    return;
  }

  ensureSdFontLoaded();

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  txt->setupCacheDir();

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  stableBookId = BookIdentity::resolveStableBookId(filePath);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "", stableBookId);
  READING_STATS.beginSession(filePath, txt->getTitle(), "", txt->getCoverBmpPath(), 0, "", 0);

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();

  ReaderUtils::requestReaderUiTransitionRefresh(renderer);

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Credit if this path did not already (early exits credit before endSession).
  // endSession is idempotent: a second call keeps lastSessionSnapshot for the
  // post-read stats banner. recordSessionEnded dedupes by snapshot serial.
  creditCurrentPageWords();

  pageOffsets.clear();
  totalBookWords = 0;
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  txt.reset();
}

void TxtReaderActivity::loop() {
  READING_STATS.tickActiveSession();
  const unsigned long nowMs = millis();

  if (waitingForConfirmSecondClick && ReaderUtils::hasNonConfirmNavigationInput(mappedInput)) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
      ReaderUtils::registerConfirmDoubleClick(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
    requestCurrentPageFullRefresh();
    return;
  }

  if (ReaderUtils::shouldToggleStatusBar(mappedInput)) {
    toggleTemporaryStatusBar();
    return;
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    creditCurrentPageWords();
    const std::string fileBrowserPath = moveCompletedBookIfEnabled();
    READING_STATS.endSession();
    ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
    showPendingAchievementPopups(renderer);
    activityManager.goToFileBrowser(fileBrowserPath);
    return;
  }

  // Short press BACK goes directly to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    exitReaderAfterOptionalCompletedMove();
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }
  if (fromTilt) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  if (prevTriggered && currentPage > 0) {
    READING_STATS.noteActivity();
    // Backward turns never credit; re-reads credit later if the reader lingers.
    currentPage--;
    pageDwell.noteEntered(currentPage, 0, millis());
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      READING_STATS.noteActivity();
      maybeCreditPageWords(currentPage);
      currentPage++;
      pageDwell.noteEntered(currentPage, 0, millis());
      requestUpdate();
    } else {
      READING_STATS.noteActivity();
      maybeCreditPageWords(currentPage);
      READING_STATS.updateProgress(100, true, "", 100);
      exitReaderAfterOptionalCompletedMove();
    }
  }
}

void TxtReaderActivity::requestCurrentPageFullRefresh() {
  READING_STATS.noteActivity();
  pendingForceFullRefresh = true;
  requestUpdate();
}

void TxtReaderActivity::toggleTemporaryStatusBar() {
  READING_STATS.noteActivity();
  statusBarTemporarilyHidden = !statusBarTemporarilyHidden;
  // Full reinit is required: status-bar height changes the viewport and linesPerPage,
  // which invalidates pageOffsets (different page breaks).
  initialized = false;
  pageOffsets.clear();
  currentPageLines.clear();
  pageDwell.clear();
  pendingForceFullRefresh = true;
  requestUpdate();
}

std::string TxtReaderActivity::moveCompletedBookIfEnabled() {
  if (!txt) {
    return "";
  }

  const std::string sourcePath = txt->getPath();
  if (!SETTINGS.moveCompletedBooks) {
    return sourcePath;
  }

  const auto* statsBook = READING_STATS.findBook(!stableBookId.empty() ? stableBookId : sourcePath);
  if (!statsBook || !statsBook->completed) {
    return sourcePath;
  }

  const std::string title = txt->getTitle();
  const std::string coverBmpPath = txt->getCoverBmpPath();
  txt.reset();

  const auto moveResult =
      CompletedBookMover::moveCompletedBookIfEnabled(sourcePath, title, "", coverBmpPath, stableBookId);
  return moveResult.moved ? moveResult.destinationPath : sourcePath;
}

void TxtReaderActivity::exitReaderAfterOptionalCompletedMove() {
  creditCurrentPageWords();
  const std::string exitPath = moveCompletedBookIfEnabled();
  exitReaderToHomeOrStats(renderer, mappedInput, exitPath);
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  const uint8_t statusBarHeight = statusBarTemporarilyHidden ? 0 : UITheme::getInstance().getStatusBarHeight();
  cachedOrientedMarginBottom += std::max(cachedScreenMargin, statusBarHeight);

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    buildPageIndex();  // builds and saves cache
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  totalBookWords = 0;
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<TextLine> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    totalBookWords += countWordsInLines(tempLines);

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages, %lu words", totalPages, static_cast<unsigned long>(totalBookWords));
  savePageIndexCache();
}

uint32_t TxtReaderActivity::countWordsInLines(const std::vector<TextLine>& lines) const {
  // Simple whitespace tokenization is enough for TXT pro-rata / average credits.
  // EPUB keeps layout-token counts; cross-format rates are intentionally coarse.
  uint32_t words = 0;
  for (const auto& line : lines) {
    bool inWord = false;
    for (const unsigned char c : line.text) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        inWord = false;
      } else if (!inWord) {
        inWord = true;
        ++words;
      }
    }
  }
  return words;
}

void TxtReaderActivity::creditCurrentPageWords() {
  maybeCreditPageWords(currentPage);
  pageDwell.clear();
}

void TxtReaderActivity::maybeCreditPageWords(const int page) {
  if (page < 0) {
    return;
  }

  const uint32_t words = averageWordsPerPage();
  const uint32_t associatedMs = pageDwell.takeCredit(page, 0, words, millis());
  if (associatedMs == 0) {
    return;
  }

  READING_STATS.noteWordsRead(words, associatedMs);
}

uint32_t TxtReaderActivity::averageWordsPerPage() const {
  if (totalPages <= 0 || totalBookWords == 0) {
    return 0;
  }
  return totalBookWords / static_cast<uint32_t>(totalPages);
}

uint32_t TxtReaderActivity::estimateRemainingWords(const int fromPage) const {
  if (fromPage < 0 || totalPages <= 0 || fromPage >= totalPages || totalBookWords == 0) {
    return 0;
  }
  // Pro-rate the book total across remaining pages (inclusive of fromPage).
  const uint32_t pagesLeft = static_cast<uint32_t>(totalPages - fromPage);
  return static_cast<uint32_t>((static_cast<uint64_t>(totalBookWords) * pagesLeft) /
                               static_cast<uint32_t>(totalPages));
}

void TxtReaderActivity::resumeAfterSubactivity() {
  READING_STATS.resumeSession();
  pageDwell.noteEntered(currentPage, 0, millis(), true);
}

void TxtReaderActivity::openReaderSubactivity(std::unique_ptr<Activity>&& activity,
                                              ActivityResultHandler onResult) {
  READING_STATS.noteActivity();
  pageDwell.clear();
  startActivityForResult(std::move(activity), [this, onResult = std::move(onResult)](const ActivityResult& result) {
    resumeAfterSubactivity();
    if (onResult) {
      onResult(result);
    }
  });
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<TextLine>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // SD-card fonts need advance metrics before wrapping text. Prime them once
  // per chunk so TXT/Markdown readers do not thrash the small overflow glyph cache.
  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x0F);
  }

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    const std::string sourceLine(reinterpret_cast<char*>(buffer + pos), displayLen);
    TextLine lineInfo =
        txt->isMarkdown() ? parseMarkdownLine(sourceLine, cachedParagraphAlignment)
                          : makePlainTextLine(sourceLine, cachedParagraphAlignment);
    if (lineInfo.text.empty() && txt->isMarkdown() && trimMarkdownWhitespace(sourceLine).empty() &&
        static_cast<int>(outLines.size()) < linesPerPage) {
      outLines.push_back(std::move(lineInfo));
      pos = lineEnd + 1;
      continue;
    }
    size_t wrappedLineStart = 0;

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Word wrap if needed
    while (wrappedLineStart < lineInfo.text.length() && static_cast<int>(outLines.size()) < linesPerPage) {
      const std::string line = lineInfo.text.substr(wrappedLineStart);
      const auto lineStyle = static_cast<EpdFontFamily::Style>(lineInfo.style);
      const int indentPx = lineInfo.indent * renderer.getSpaceWidth(cachedFontId, lineStyle) * 2;
      const int lineViewportWidth = std::max(1, viewportWidth - indentPx);
      int lineWidth = getTextLineWidth(renderer, cachedFontId, sliceTextLine(lineInfo, wrappedLineStart, line.length()));

      if (lineWidth <= lineViewportWidth) {
        TextLine displayLine = sliceTextLine(lineInfo, wrappedLineStart, line.length());
        outLines.push_back(std::move(displayLine));
        lineBytePos = displayLen;  // Consumed entire display content
        wrappedLineStart = lineInfo.text.length();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 &&
             getTextLineWidth(renderer, cachedFontId, sliceTextLine(lineInfo, wrappedLineStart, breakPos)) >
                 lineViewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          // Make sure we don't break in the middle of a UTF-8 sequence
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      TextLine displayLine = sliceTextLine(lineInfo, wrappedLineStart, breakPos);
      outLines.push_back(std::move(displayLine));

      // Skip space at break point
      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      wrappedLineStart += skipChars;
    }

    // Determine how much of the source buffer we consumed
    if (wrappedLineStart >= lineInfo.text.length()) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
}

void TxtReaderActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.text.empty()) {
        const auto lineStyle = static_cast<EpdFontFamily::Style>(line.style);
        const int indentPx = line.indent * renderer.getSpaceWidth(cachedFontId, lineStyle) * 2;
        int x = cachedOrientedMarginLeft;

        // Apply text alignment
        switch (line.alignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set to left margin
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = getTextLineWidth(renderer, cachedFontId, line);
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = getTextLineWidth(renderer, cachedFontId, line);
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            // For plain text, justified is treated as left-aligned
            // (true justification would require word spacing adjustments)
            break;
        }
        if (line.alignment == CrossPointSettings::LEFT_ALIGN || line.alignment == CrossPointSettings::JUSTIFIED) {
          x += indentPx;
        }

        if (line.spans.empty()) {
          renderer.drawText(cachedFontId, x, y, line.text.c_str(), true, lineStyle);
        } else {
          int spanX = x;
          for (const auto& span : line.spans) {
            const auto spanStyle = static_cast<EpdFontFamily::Style>(span.style);
            renderer.drawText(cachedFontId, spanX, y, span.text.c_str(), true, spanStyle);
            spanX += renderer.getTextAdvanceX(cachedFontId, span.text.c_str(), spanStyle);
          }
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();  // scan pass — text accumulated, no drawing
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  if (currentPage >= 0) {
    pageDwell.noteEntered(currentPage, 0, millis());
  }
  renderStatusBar();

  const bool forceFullRefresh = pendingForceFullRefresh;
  pendingForceFullRefresh = false;
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, forceFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
  // scope destructor clears font cache via FontCacheManager
}

void TxtReaderActivity::renderStatusBar() const {
  if (statusBarTemporarilyHidden) {
    return;
  }

  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = txt->getTitle();
  }

  // Sized for multi-byte unit suffixes; formatCompactDuration fails closed if still too small.
  char chapterTimeBuf[24] = {};
  const char* chapterTimeEstimate = nullptr;
  const double wordsPerMs = READING_STATS.getEffectiveWordsPerMs();
  if (ChapterTimeEstimate::statusBarWantsChapterTime() && wordsPerMs > 0.0 &&
      ChapterTimeEstimate::formatRemainingFromRate(estimateRemainingWords(currentPage), wordsPerMs, chapterTimeBuf,
                                                   sizeof(chapterTimeBuf))) {
    chapterTimeEstimate = chapterTimeBuf;
  }

  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title, 0, 0, true, chapterTimeEstimate);
}

void TxtReaderActivity::saveProgress() const {
  const uint8_t progressPercent =
      totalPages > 0 ? static_cast<uint8_t>(std::min(100, ((currentPage + 1) * 100) / totalPages)) : 0;
  READING_STATS.updateProgress(progressPercent, totalPages > 0 && currentPage + 1 >= totalPages, "", progressPercent);

  std::string progressPath = getStableProgressPath(stableBookId);
  if (!progressPath.empty()) {
    BookIdentity::ensureStableDataDir(stableBookId);
  } else {
    progressPath = getLegacyProgressPath(*txt);
  }
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  ProgressFile::writeAtomicPath("TRS", progressPath, data, sizeof(data));
}

void TxtReaderActivity::loadProgress() {
  FsFile f;
  bool loadedFromLegacy = false;
  const std::string stableProgressPath = getStableProgressPath(stableBookId);
  const std::string legacyProgressPath = getLegacyProgressPath(*txt);
  const std::string progressPath =
      (!stableProgressPath.empty() && Storage.exists(stableProgressPath.c_str())) ? stableProgressPath : legacyProgressPath;
  if (progressPath == legacyProgressPath) {
    loadedFromLegacy = !stableProgressPath.empty() && Storage.exists(legacyProgressPath.c_str());
  }
  if (Storage.openFileForRead("TRS", progressPath, f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
    f.close();
    if (loadedFromLegacy) {
      saveProgress();
    }
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  // Cache file format (serialization module, little-endian POD writes):
  // - uint32_t: magic "TXTI"
  // - uint8_t: cache version
  // - uint32_t: file size (to validate cache)
  // - int32_t: viewport width
  // - int32_t: lines per page
  // - int32_t: font ID
  // - int32_t: screen margin
  // - uint8_t: paragraph alignment
  // - uint32_t: total pages count
  // - N * uint32_t: page offsets
  // - uint32_t: totalBookWords

  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  totalBookWords = 0;
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }
  serialization::readPod(f, totalBookWords);

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages, %lu words", totalPages,
          static_cast<unsigned long>(totalBookWords));
  return true;
}

void TxtReaderActivity::savePageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }
  serialization::writePod(f, totalBookWords);

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
