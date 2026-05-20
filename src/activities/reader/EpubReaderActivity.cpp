#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>

#include <algorithm>
#include <iterator>
#include <limits>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "AchievementsStore.h"
#include "BookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "QrDisplayActivity.h"
#include "ReaderJumpMenuActivity.h"
#include "ReaderNavigationMenuActivity.h"
#include "ReaderQuickSettingsActivity.h"
#include "ReaderRecentBooksActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookIdentity.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/ScreenshotUtil.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long bookmarkToggleMs = 700;
// pages per minute, first item is 1 to prevent division by zero if accessed
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr int QS_TAB_READER = 0;
constexpr int QS_TAB_DISPLAY = 1;
constexpr int QS_TAB_COUNT = 2;
constexpr int QS_READER_ITEM_COUNT = 4;
constexpr int QS_DISPLAY_ITEM_COUNT = 3;
constexpr StrId QS_TAB_LABELS[QS_TAB_COUNT] = {StrId::STR_CAT_READER, StrId::STR_CAT_DISPLAY};
constexpr StrId QS_READER_LABELS[QS_READER_ITEM_COUNT] = {StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                                          StrId::STR_SCREEN_MARGIN, StrId::STR_PARA_ALIGNMENT};
constexpr StrId QS_DISPLAY_LABELS[QS_DISPLAY_ITEM_COUNT] = {StrId::STR_DARK_MODE, StrId::STR_TEXT_DARKNESS,
                                                            StrId::STR_READER_REFRESH_MODE};
constexpr StrId QS_FONT_SIZE_LABELS[] = {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                                         StrId::STR_X_LARGE};
constexpr StrId QS_LINE_SPACING_LABELS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId QS_ALIGNMENT_LABELS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER,
                                         StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE};
constexpr StrId QS_DARK_MODE_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON};
constexpr StrId QS_TEXT_DARKNESS_LABELS[] = {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK,
                                             StrId::STR_EXTRA_DARK};
constexpr StrId QS_REFRESH_MODE_LABELS[] = {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST,
                                            StrId::STR_REFRESH_MODE_HALF, StrId::STR_REFRESH_MODE_FULL};

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

uint8_t wrapSettingValue(const uint8_t value, const int direction, const uint8_t count) {
  if (direction > 0) {
    return static_cast<uint8_t>((value + 1) % count);
  }
  return value == 0 ? static_cast<uint8_t>(count - 1) : static_cast<uint8_t>(value - 1);
}

std::string quickEnumValue(const uint8_t value, const StrId* labels, const uint8_t count) {
  return I18N.get(labels[std::min<uint8_t>(value, count - 1)]);
}

std::string getStatsChapterTitle(Epub& epub, const int spineIndex) {
  int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
  if (tocIndex < 0) {
    int nearestTocIndex = -1;
    int nearestSpineIndex = -1;
    for (int index = 0; index < epub.getTocItemsCount(); ++index) {
      const int tocSpineIndex = epub.getSpineIndexForTocIndex(index);
      if (tocSpineIndex <= spineIndex && tocSpineIndex >= nearestSpineIndex) {
        nearestSpineIndex = tocSpineIndex;
        nearestTocIndex = index;
      }
    }
    tocIndex = nearestTocIndex;
  }

  if (tocIndex < 0) {
    return "";
  }

  const auto tocItem = epub.getTocItem(tocIndex);
  return tocItem.title;
}

uint8_t getStatsChapterProgressPercent(const int currentPage, const int pageCount) {
  if (pageCount <= 0) {
    return 0;
  }

  return static_cast<uint8_t>(
      clampPercent(static_cast<int>((static_cast<float>(currentPage + 1) / static_cast<float>(pageCount)) * 100.0f + 0.5f)));
}

void markStatsCompletedAtEnd(Epub& epub, int spineIndex) {
  const int spineCount = epub.getSpineItemsCount();
  if (spineCount <= 0) {
    READING_STATS.updateProgress(100, true, "", 100);
    return;
  }

  if (spineIndex >= spineCount) {
    spineIndex = spineCount - 1;
  } else if (spineIndex < 0) {
    spineIndex = 0;
  }

  READING_STATS.updateProgress(100, true, getStatsChapterTitle(epub, spineIndex), 100);
}

std::string getStableProgressPath(const std::string& bookId) {
  return BookIdentity::getStableDataFilePath(bookId, "epub_progress.bin");
}

std::string getLegacyProgressPath(Epub& epub) { return epub.getCachePath() + "/progress.bin"; }

std::string extractBookmarkSnippet(Section& section) {
  auto page = section.loadPageFromSectionFile();
  if (!page) {
    return "";
  }

  std::string snippet;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) {
      continue;
    }

    const auto& line = static_cast<const PageLine&>(*element);
    if (!line.getBlock()) {
      continue;
    }

    for (const auto& word : line.getBlock()->getWords()) {
      if (!snippet.empty()) {
        snippet += ' ';
      }
      snippet += word;
      if (snippet.size() >= 80) {
        return snippet;
      }
    }
  }

  return snippet;
}

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

bool writeReaderProgressCache(const std::string& cachePath, const int spineIndex, const int currentPage,
                              const int pageCount) {
  FsFile f;
  const std::string progressPath = cachePath + "/progress.bin";
  if (!Storage.openFileForWrite("ERS", progressPath, f)) {
    LOG_ERR("ERS", "Failed to open progress cache for sync restore: %s", progressPath.c_str());
    return false;
  }
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = currentPage & 0xFF;
  data[3] = (currentPage >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  f.write(data, 6);
  f.close();
  return true;
}

bool writeReaderProgressFile(const std::string& progressPath, const int spineIndex, const int currentPage,
                             const int pageCount) {
  FsFile f;
  if (!Storage.openFileForWrite("ERS", progressPath, f)) {
    LOG_ERR("ERS", "Failed to open progress file: %s", progressPath.c_str());
    return false;
  }
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = currentPage & 0xFF;
  data[3] = (currentPage >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  f.write(data, 6);
  f.close();
  return true;
}

}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderMode(true);

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  sdFontSystem.ensureLoaded(renderer);

  epub->setupCacheDir();
  applyPendingSyncSession();
  stableBookId = BookIdentity::resolveStableBookId(epub->getPath());
  bookmarkStore.load(epub->getCachePath(), stableBookId);

  FsFile f;
  bool loadedFromLegacy = false;
  const std::string stableProgressPath = getStableProgressPath(stableBookId);
  const std::string legacyProgressPath = getLegacyProgressPath(*epub);
  const std::string progressPath =
      (!stableProgressPath.empty() && Storage.exists(stableProgressPath.c_str())) ? stableProgressPath : legacyProgressPath;
  if (progressPath == legacyProgressPath) {
    loadedFromLegacy = !stableProgressPath.empty() && Storage.exists(legacyProgressPath.c_str());
  }
  if (Storage.openFileForRead("ERS", progressPath, f)) {
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
    f.close();
    if (loadedFromLegacy) {
      saveProgress(currentSpineIndex, nextPageNumber, cachedChapterTotalPageCount);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  if (initialBookmarkSpineIndex >= 0) {
    const int maxSpineIndex = std::max(0, epub->getSpineItemsCount() - 1);
    currentSpineIndex = std::min(initialBookmarkSpineIndex, maxSpineIndex);
    nextPageNumber = std::max(0, initialBookmarkPage);
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = 0;
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(), stableBookId);
  READING_STATS.beginSession(
      epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getCoverBmpPath(),
      clampPercent(static_cast<int>(epub->calculateProgress(currentSpineIndex, 0.0f) * 100.0f + 0.5f)),
      getStatsChapterTitle(*epub, currentSpineIndex), 0);

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  Activity::onExit();
  mappedInput.setReaderMode(false);

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  bookmarkStore.save();
  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  READING_STATS.tickActiveSession();
  const unsigned long nowMs = millis();

  if (quickSettingsOpen) {
    if (handleQuickSettingsInput()) {
      return;
    }
  }

  if (executeShortPowerButtonAction() || executeLongPowerButtonAction()) {
    return;
  }
  if (consumeLongPowerButtonRelease()) {
    return;
  }

  if (waitingForConfirmSecondClick && ReaderUtils::hasNonConfirmNavigationInput(mappedInput)) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      automaticPageTurnActive = false;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true);
      return;
    }
  }

  if (handleImmediateConfirmLongPress()) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && confirmLongPressHandled) {
    confirmLongPressHandled = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= bookmarkToggleMs) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
    if (SETTINGS.longPressMenuAction != CrossPointSettings::LONG_MENU_OFF) {
      executeReaderQuickAction(static_cast<CrossPointSettings::LONG_PRESS_MENU_ACTION>(SETTINGS.longPressMenuAction));
      return;
    }
    if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
      READING_STATS.noteActivity();
      const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
      const uint16_t pageNumber = static_cast<uint16_t>(section->currentPage);
      const bool wasBookmarked = bookmarkStore.has(spineIndex, pageNumber);
      const std::string snippet = wasBookmarked ? "" : extractBookmarkSnippet(*section);
      const bool addedBookmark = bookmarkStore.toggle(spineIndex, pageNumber, snippet);
      bookmarkStore.save();
      if (addedBookmark && epub && !READING_STATS.shouldIgnorePath(epub->getPath())) {
        ACHIEVEMENTS.recordBookmarkAdded();
      }
      const bool showedAchievement = showPendingAchievementPopups(renderer);
      if (!showedAchievement) {
        GUI.drawPopup(renderer, addedBookmark ? tr(STR_BOOKMARK_ADDED) : tr(STR_BOOKMARK_REMOVED));
        renderer.displayBuffer();
        delay(500);
      }
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ReaderUtils::registerConfirmDoubleClick(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
      requestCurrentPageFullRefresh();
      return;
    }
  }

  // Enter reader menu activity.
  if (ReaderUtils::hasPendingConfirmSingleClickExpired(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
    READING_STATS.noteActivity();
    const int currentPage = section ? section->currentPage + 1 : 0;
    const int totalPages = section ? section->pageCount : 0;
    float bookProgress = 0.0f;
    if (epub->getBookSize() > 0 && section && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
    }
    const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
    startActivityForResult(std::make_unique<EpubReaderMenuActivity>(
                               renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
                               SETTINGS.orientation, !currentPageFootnotes.empty(), !bookmarkStore.isEmpty()),
                           [this](const ActivityResult& result) {
                             READING_STATS.resumeSession();
                             // Always apply orientation change even if the menu was cancelled
                             const auto& menu = std::get<MenuResult>(result.data);
                             applyOrientation(menu.orientation);
                             toggleAutoPageTurn(menu.pageTurnOption);
                             if (!result.isCancelled) {
                               onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                             }
                           });
  }

  // Long press BACK opens Recent Books directly.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && !backLongPressHandled &&
      mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    backLongPressHandled = true;
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
    openRecentBooksSwitcher();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backLongPressHandled) {
    backLongPressHandled = false;
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    exitReaderToHomeOrStats(renderer, mappedInput, epub ? epub->getPath() : "");
    return;
  }

  if (handleTurnButtonLongPressRelease()) {
    return;
  }

  if (handleImmediateTurnButtonLongPress()) {
    return;
  }

  auto [prevTriggered, nextTriggered, fromSideBtn, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }
  if (fromTilt) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  // At end of the book, forward button goes home and back button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (nextTriggered) {
      exitReaderToHomeOrStats(renderer, mappedInput, epub ? epub->getPath() : "");
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const bool turnButtonLongPress = !fromTilt && mappedInput.getHeldTime() > skipChapterMs;
  const bool skipChapter =
      turnButtonLongPress && (fromSideBtn ? SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP
                                          : SETTINGS.longPressButtonBehavior ==
                                                CrossPointSettings::LONG_PRESS_CHAPTER_SKIP);

  // Don't skip chapter after screenshot
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  if (turnButtonLongPress && !skipChapter) {
    handleTurnButtonLongPress(fromSideBtn);
    return;
  }

  if (skipChapter) {
    READING_STATS.noteActivity();
    lastPageTurnTime = millis();
    skipToChapter(nextTriggered);
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

void EpubReaderActivity::requestCurrentPageFullRefresh() {
  READING_STATS.noteActivity();
  pendingForceFullRefresh = true;
  requestUpdate();
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled && currentSpineIndex != std::get<ChapterResult>(result.data).spineIndex) {
              RenderLock lock(*this);
              currentSpineIndex = std::get<ChapterResult>(result.data).spineIndex;
              nextPageNumber = 0;
              section.reset();
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               READING_STATS.resumeSession();
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<BookmarksActivity>(
              renderer, mappedInput, bookmarkStore.getAll(), epub, "",
              [this](const BookmarkStore::Bookmark& bookmark) {
                const bool removed = bookmarkStore.remove(bookmark.spineIndex, bookmark.pageNumber);
                if (removed) {
                  bookmarkStore.save();
                }
                return removed;
              }),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              const auto& bookmark = std::get<BookmarkResult>(result.data);
              if (currentSpineIndex != bookmark.spineIndex || !section ||
                  section->currentPage != static_cast<int>(bookmark.page)) {
                RenderLock lock(*this);
                currentSpineIndex = bookmark.spineIndex;
                nextPageNumber = static_cast<int>(bookmark.page);
                section.reset();
              }
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
      // The menu returns auto-page selection through MenuResult::pageTurnOption.
      requestUpdate();
      break;
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
      // The menu returns orientation changes through MenuResult::orientation.
      requestUpdate();
      break;
    case EpubReaderMenuActivity::MenuAction::STATUS_BAR: {
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               READING_STATS.resumeSession();
                               section.reset();
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::QUICK_SETTINGS: {
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<ReaderQuickSettingsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               READING_STATS.resumeSession();
                               sdFontSystem.ensureLoaded(renderer);
                               if (section) {
                                 cachedSpineIndex = currentSpineIndex;
                                 cachedChapterTotalPageCount = section->pageCount;
                                 nextPageNumber = section->currentPage;
                               }
                               section.reset();
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        auto p = section->loadPageFromSectionFile();
        if (p) {
          std::string fullText;
          for (const auto& el : p->elements) {
            if (el->getTag() == TAG_PageLine) {
              const auto& line = static_cast<const PageLine&>(*el);
              if (line.getBlock()) {
                const auto& words = line.getBlock()->getWords();
                for (const auto& w : words) {
                  if (!fullText.empty()) fullText += " ";
                  fullText += w;
                }
              }
            }
          }
          if (!fullText.empty()) {
            READING_STATS.noteActivity();
            startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                   [this](const ActivityResult& result) { READING_STATS.resumeSession(); });
            break;
          }
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          saveProgress(backupSpine, backupPage, backupPageCount);
          if (!bookmarkStore.isEmpty()) {
            bookmarkStore.markDirty();
            bookmarkStore.save();
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      if (KOREADER_STORE.hasCredentials()) {
        READING_STATS.noteActivity();
        launchKOReaderSync(SyncLaunchMode::COMPARE);
      }
      break;
    }
  }
}

void EpubReaderActivity::openReaderNavigationMenu() {
  READING_STATS.noteActivity();
  startActivityForResult(std::make_unique<ReaderNavigationMenuActivity>(renderer, mappedInput, epub->getTitle()),
                         [this](const ActivityResult& result) {
                           READING_STATS.resumeSession();
                           backLongPressHandled = false;
                           if (!result.isCancelled) {
                             handleReaderNavigationAction(std::get<MenuResult>(result.data).action);
                           } else {
                             requestUpdate();
                           }
                         });
}

void EpubReaderActivity::openJumpMenu() {
  READING_STATS.noteActivity();
  startActivityForResult(std::make_unique<ReaderJumpMenuActivity>(
                             renderer, mappedInput, tr(STR_JUMP_MENU), epub->getTocItemsCount() > 0,
                             !bookmarkStore.isEmpty()),
                         [this](const ActivityResult& result) {
                           READING_STATS.resumeSession();
                           if (!result.isCancelled) {
                             handleJumpMenuAction(std::get<MenuResult>(result.data).action);
                           } else {
                             requestUpdate();
                           }
                         });
}

void EpubReaderActivity::openRecentBooksSwitcher() {
  READING_STATS.noteActivity();
  startActivityForResult(std::make_unique<ReaderRecentBooksActivity>(renderer, mappedInput, epub->getPath()),
                         [this](const ActivityResult& result) {
                           backLongPressHandled = false;
                           if (!result.isCancelled) {
                             const std::string path = std::get<KeyboardResult>(result.data).text;
                             if (!path.empty()) {
                               activityManager.goToReader(path);
                               return;
                             }
                           }
                           READING_STATS.resumeSession();
                           requestUpdate();
                         });
}

void EpubReaderActivity::handleReaderNavigationAction(const int action) {
  switch (static_cast<ReaderNavigationMenuActivity::Action>(action)) {
    case ReaderNavigationMenuActivity::Action::OPEN_RECENT_BOOKS:
      openRecentBooksSwitcher();
      break;
  }
}

void EpubReaderActivity::handleJumpMenuAction(const int action) {
  switch (static_cast<ReaderJumpMenuActivity::Action>(action)) {
    case ReaderJumpMenuActivity::Action::CHAPTERS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER);
      break;
    case ReaderJumpMenuActivity::Action::PERCENT:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT);
      break;
    case ReaderJumpMenuActivity::Action::BOOKMARKS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::BOOKMARKS);
      break;
    case ReaderJumpMenuActivity::Action::BACK_TO_READING:
      requestUpdate();
      break;
  }
}

void EpubReaderActivity::openQuickSettingsOverlay() {
  quickSettingsOpen = true;
  quickSettingsTabFocused = true;
  quickSettingsTab = QS_TAB_READER;
  quickSettingsItem = 0;
  quickSettingsNeedsReflow = false;
  renderQuickSettingsOverlay();
}

int EpubReaderActivity::getQuickSettingsItemCount() const {
  return quickSettingsTab == QS_TAB_READER ? QS_READER_ITEM_COUNT : QS_DISPLAY_ITEM_COUNT;
}

const char* EpubReaderActivity::getQuickSettingLabel(const int tab, const int index) const {
  return I18N.get(tab == QS_TAB_READER ? QS_READER_LABELS[index] : QS_DISPLAY_LABELS[index]);
}

std::string EpubReaderActivity::getQuickSettingValue(const int tab, const int index) const {
  if (tab == QS_TAB_READER) {
    switch (index) {
      case 0:
        return quickEnumValue(SETTINGS.fontSize, QS_FONT_SIZE_LABELS, CrossPointSettings::FONT_SIZE_COUNT);
      case 1:
        return quickEnumValue(SETTINGS.lineSpacing, QS_LINE_SPACING_LABELS, CrossPointSettings::LINE_COMPRESSION_COUNT);
      case 2:
        return std::to_string(SETTINGS.screenMargin);
      case 3:
        return quickEnumValue(SETTINGS.paragraphAlignment, QS_ALIGNMENT_LABELS,
                              CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
      default:
        return "";
    }
  }

  switch (index) {
    case 0:
      return I18N.get(QS_DARK_MODE_LABELS[SETTINGS.darkMode ? 1 : 0]);
    case 1:
      return quickEnumValue(SETTINGS.textDarkness, QS_TEXT_DARKNESS_LABELS, CrossPointSettings::TEXT_DARKNESS_COUNT);
    case 2:
      return quickEnumValue(SETTINGS.readerRefreshMode, QS_REFRESH_MODE_LABELS,
                            CrossPointSettings::READER_REFRESH_MODE_COUNT);
    default:
      return "";
  }
}

void EpubReaderActivity::adjustQuickSetting(const int direction) {
  if (quickSettingsTab == QS_TAB_READER) {
    switch (quickSettingsItem) {
      case 0:
        SETTINGS.fontSize = wrapSettingValue(SETTINGS.fontSize, direction, CrossPointSettings::FONT_SIZE_COUNT);
        quickSettingsNeedsReflow = true;
        break;
      case 1:
        SETTINGS.lineSpacing =
            wrapSettingValue(SETTINGS.lineSpacing, direction, CrossPointSettings::LINE_COMPRESSION_COUNT);
        quickSettingsNeedsReflow = true;
        break;
      case 2:
        SETTINGS.screenMargin =
            direction > 0 ? std::min<uint8_t>(40, SETTINGS.screenMargin + 5)
                          : (SETTINGS.screenMargin <= 5 ? 40 : static_cast<uint8_t>(SETTINGS.screenMargin - 5));
        quickSettingsNeedsReflow = true;
        break;
      case 3:
        SETTINGS.paragraphAlignment =
            wrapSettingValue(SETTINGS.paragraphAlignment, direction, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
        quickSettingsNeedsReflow = true;
        break;
    }
  } else {
    switch (quickSettingsItem) {
      case 0:
        SETTINGS.darkMode = SETTINGS.darkMode ? 0 : 1;
        renderer.setDarkMode(SETTINGS.darkMode);
        break;
      case 1:
        SETTINGS.textDarkness = wrapSettingValue(SETTINGS.textDarkness, direction, CrossPointSettings::TEXT_DARKNESS_COUNT);
        renderer.setTextDarkness(SETTINGS.textDarkness);
        quickSettingsNeedsReflow = true;
        break;
      case 2:
        SETTINGS.readerRefreshMode =
            wrapSettingValue(SETTINGS.readerRefreshMode, direction, CrossPointSettings::READER_REFRESH_MODE_COUNT);
        break;
    }
  }

  SETTINGS.saveToFile();
  renderQuickSettingsOverlay();
}

void EpubReaderActivity::closeQuickSettingsOverlay() {
  quickSettingsOpen = false;
  if (quickSettingsNeedsReflow) {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
    pagesUntilFullRefresh = 0;
  }
  requestUpdate();
}

bool EpubReaderActivity::handleQuickSettingsInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!quickSettingsTabFocused) {
      quickSettingsTabFocused = true;
      renderQuickSettingsOverlay();
      return true;
    }
    closeQuickSettingsOverlay();
    return true;
  }

  if (quickSettingsTabFocused) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      quickSettingsTab = quickSettingsTab == QS_TAB_READER ? QS_TAB_DISPLAY : QS_TAB_READER;
      quickSettingsItem = 0;
      renderQuickSettingsOverlay();
      return true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      quickSettingsTabFocused = false;
      renderQuickSettingsOverlay();
      return true;
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    adjustQuickSetting(1);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    adjustQuickSetting(-1);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    quickSettingsItem = ButtonNavigator::nextIndex(quickSettingsItem, getQuickSettingsItemCount());
    renderQuickSettingsOverlay();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    quickSettingsItem = ButtonNavigator::previousIndex(quickSettingsItem, getQuickSettingsItemCount());
    renderQuickSettingsOverlay();
    return true;
  }
  return true;
}

void EpubReaderActivity::renderQuickSettingsOverlay() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int overlayH = std::min(310, h - 40);
  const int overlayY = h - overlayH;
  renderer.fillRect(0, overlayY, w, overlayH, false);
  renderer.drawRect(0, overlayY, w, overlayH);

  const int tabY = overlayY + 12;
  const int tabH = 34;
  const int tabW = w / QS_TAB_COUNT;
  for (int tab = 0; tab < QS_TAB_COUNT; ++tab) {
    const bool active = tab == quickSettingsTab;
    const int tabX = tab * tabW;
    if (active) {
      renderer.fillRectDither(tabX + 8, tabY, tabW - 16, tabH, Color::LightGray);
    }
    renderer.drawRect(tabX + 8, tabY, tabW - 16, tabH);
    const char* label = I18N.get(QS_TAB_LABELS[tab]);
    const auto fontStyle = active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, label, fontStyle);
    renderer.drawText(UI_10_FONT_ID, tabX + (tabW - textW) / 2, tabY + 8, label, true,
                      active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (quickSettingsTabFocused && active) {
      renderer.drawRect(tabX + 10, tabY + 2, tabW - 20, tabH - 4);
    }
  }

  const int rowTop = tabY + tabH + 14;
  const int rowH = 42;
  const int itemCount = getQuickSettingsItemCount();
  for (int i = 0; i < itemCount; ++i) {
    const int y = rowTop + i * rowH;
    const bool focused = !quickSettingsTabFocused && i == quickSettingsItem;
    if (focused) {
      renderer.fillRectDither(12, y - 4, w - 24, rowH - 2, Color::LightGray);
    }
    renderer.drawText(UI_10_FONT_ID, 24, y + 6, getQuickSettingLabel(quickSettingsTab, i), true,
                      focused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    std::string value = getQuickSettingValue(quickSettingsTab, i);
    if (focused) {
      value = "< " + value + " >";
    }
    value = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), 170, focused ? EpdFontFamily::BOLD
                                                                              : EpdFontFamily::REGULAR);
    const int valueW = renderer.getTextWidth(UI_10_FONT_ID, value.c_str(), focused ? EpdFontFamily::BOLD
                                                                                   : EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, w - 24 - valueW, y + 6, value.c_str(), true,
                      focused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawLine(12, y + rowH - 3, w - 12, y + rowH - 3);
  }

  const auto labels =
      quickSettingsTabFocused ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                              : mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubReaderActivity::cycleReaderFontFamily() {
  SETTINGS.fontFamily = (SETTINGS.fontFamily + 1) % CrossPointSettings::FONT_FAMILY_COUNT;
  SETTINGS.saveToFile();
  sdFontSystem.ensureLoaded(renderer);
  if (section) {
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }
  section.reset();
  requestUpdate();
}

void EpubReaderActivity::cycleReaderFontSize() {
  SETTINGS.fontSize = (SETTINGS.fontSize + 1) % CrossPointSettings::FONT_SIZE_COUNT;
  SETTINGS.saveToFile();
  if (section) {
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }
  section.reset();
  requestUpdate();
}

void EpubReaderActivity::toggleCurrentPageBookmark() {
  if (!section || section->currentPage < 0 || section->currentPage >= section->pageCount) {
    return;
  }

  READING_STATS.noteActivity();
  const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
  const uint16_t pageNumber = static_cast<uint16_t>(section->currentPage);
  const bool wasBookmarked = bookmarkStore.has(spineIndex, pageNumber);
  const std::string snippet = wasBookmarked ? "" : extractBookmarkSnippet(*section);
  const bool addedBookmark = bookmarkStore.toggle(spineIndex, pageNumber, snippet);
  bookmarkStore.save();
  if (addedBookmark && epub && !READING_STATS.shouldIgnorePath(epub->getPath())) {
    ACHIEVEMENTS.recordBookmarkAdded();
  }
  GUI.drawPopup(renderer, addedBookmark ? tr(STR_BOOKMARK_ADDED) : tr(STR_BOOKMARK_REMOVED));
  renderer.displayBuffer();
  delay(500);
  requestUpdate();
}

void EpubReaderActivity::markCurrentBookFinished() {
  if (!epub) {
    return;
  }

  markStatsCompletedAtEnd(*epub, section ? currentSpineIndex : epub->getSpineItemsCount() - 1);
  GUI.drawPopup(renderer, tr(STR_MARK_FINISHED));
  renderer.displayBuffer();
  delay(500);
  requestUpdate();
}

bool EpubReaderActivity::handleImmediateConfirmLongPress() {
  if (confirmLongPressHandled || SETTINGS.longPressMenuAction == CrossPointSettings::LONG_MENU_OFF ||
      !mappedInput.isPressed(MappedInputManager::Button::Confirm) || mappedInput.getHeldTime() < bookmarkToggleMs) {
    return false;
  }

  confirmLongPressHandled = true;
  waitingForConfirmSecondClick = false;
  firstConfirmClickMs = 0UL;
  executeReaderQuickAction(static_cast<CrossPointSettings::LONG_PRESS_MENU_ACTION>(SETTINGS.longPressMenuAction));
  return true;
}

void EpubReaderActivity::executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action) {
  switch (action) {
    case CrossPointSettings::LONG_MENU_SLEEP:
      activityManager.goToSleep();
      break;
    case CrossPointSettings::LONG_MENU_CHANGE_FONT:
      cycleReaderFontFamily();
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_BIONIC:
      SETTINGS.bionicReading = (SETTINGS.bionicReading + 1) % CrossPointSettings::BIONIC_READING_MODE_COUNT;
      SETTINGS.saveToFile();
      if (section) {
        cachedSpineIndex = currentSpineIndex;
        cachedChapterTotalPageCount = section->pageCount;
        nextPageNumber = section->currentPage;
      }
      section.reset();
      requestUpdate();
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_BOOKMARK:
      toggleCurrentPageBookmark();
      break;
    case CrossPointSettings::LONG_MENU_REFRESH_SCREEN:
      requestCurrentPageFullRefresh();
      break;
    case CrossPointSettings::LONG_MENU_SYNC_PROGRESS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SYNC);
      break;
    case CrossPointSettings::LONG_MENU_MARK_FINISHED:
      markCurrentBookFinished();
      break;
    case CrossPointSettings::LONG_MENU_READING_STATS:
      if (epub) {
        startActivityForResult(
            std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, epub->getPath(),
                                                         ReadingStatsDetailContext{true}),
            [this](const ActivityResult&) {
              READING_STATS.resumeSession();
              requestUpdate();
            });
      }
      break;
    case CrossPointSettings::LONG_MENU_SCREENSHOT:
      pendingScreenshot = true;
      requestUpdate();
      break;
    case CrossPointSettings::LONG_MENU_CYCLE_PAGE_TURN:
      toggleAutoPageTurn(automaticPageTurnActive ? 0 : 1);
      requestUpdate();
      break;
    case CrossPointSettings::LONG_MENU_FILE_TRANSFER:
      if (epub && section) {
        saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
      }
      activityManager.goToFileTransfer();
      break;
    case CrossPointSettings::LONG_MENU_OFF:
    default:
      break;
  }
}

CrossPointSettings::LONG_PRESS_MENU_ACTION powerActionToMenuAction(const CrossPointSettings::SHORT_PWRBTN action) {
  switch (action) {
    case CrossPointSettings::SHORT_PWRBTN::SLEEP:
      return CrossPointSettings::LONG_MENU_SLEEP;
    case CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH:
      return CrossPointSettings::LONG_MENU_REFRESH_SCREEN;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_FONT:
      return CrossPointSettings::LONG_MENU_CHANGE_FONT;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_BIONIC_READING:
      return CrossPointSettings::LONG_MENU_TOGGLE_BIONIC;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_BOOKMARK:
      return CrossPointSettings::LONG_MENU_TOGGLE_BOOKMARK;
    case CrossPointSettings::SHORT_PWRBTN::SYNC_PROGRESS:
      return CrossPointSettings::LONG_MENU_SYNC_PROGRESS;
    case CrossPointSettings::SHORT_PWRBTN::MARK_FINISHED:
      return CrossPointSettings::LONG_MENU_MARK_FINISHED;
    case CrossPointSettings::SHORT_PWRBTN::OPEN_READING_STATS:
      return CrossPointSettings::LONG_MENU_READING_STATS;
    case CrossPointSettings::SHORT_PWRBTN::SCREENSHOT:
      return CrossPointSettings::LONG_MENU_SCREENSHOT;
    case CrossPointSettings::SHORT_PWRBTN::CYCLE_PAGE_TURN:
      return CrossPointSettings::LONG_MENU_CYCLE_PAGE_TURN;
    case CrossPointSettings::SHORT_PWRBTN::FILE_TRANSFER:
      return CrossPointSettings::LONG_MENU_FILE_TRANSFER;
    default:
      return CrossPointSettings::LONG_MENU_OFF;
  }
}

bool EpubReaderActivity::executeShortPowerButtonAction() {
  if (!mappedInput.wasReleased(MappedInputManager::Button::Power) ||
      mappedInput.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration()) {
    return false;
  }

  const auto action = static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn);
  const auto menuAction = powerActionToMenuAction(action);
  if (menuAction == CrossPointSettings::LONG_MENU_OFF) {
    return false;
  }
  executeReaderQuickAction(menuAction);
  return true;
}

bool EpubReaderActivity::consumeLongPowerButtonRelease() {
  if (!mappedInput.wasReleased(MappedInputManager::Button::Power) || !longPowerButtonHandled) {
    return false;
  }

  longPowerButtonHandled = false;
  return true;
}

bool EpubReaderActivity::consumeLongPowerButtonHold() {
  if (longPowerButtonHandled || !mappedInput.isPressed(MappedInputManager::Button::Power) ||
      mappedInput.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
    return false;
  }

  longPowerButtonHandled = true;
  return true;
}

bool EpubReaderActivity::executeLongPowerButtonAction() {
  if (SETTINGS.longPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN || !consumeLongPowerButtonHold()) {
    return false;
  }

  const auto menuAction =
      powerActionToMenuAction(static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn));
  if (menuAction == CrossPointSettings::LONG_MENU_OFF) {
    return false;
  }
  executeReaderQuickAction(menuAction);
  return true;
}

bool EpubReaderActivity::handleTurnButtonLongPressRelease() {
  if (!turnButtonLongPressHandled) {
    return false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    turnButtonLongPressHandled = false;
    return true;
  }

  return false;
}

bool EpubReaderActivity::handleImmediateTurnButtonLongPress() {
  if (turnButtonLongPressHandled || mappedInput.getHeldTime() <= skipChapterMs) {
    return false;
  }

  const bool sidePrev = mappedInput.isPressed(MappedInputManager::Button::PageBack);
  const bool sideNext = mappedInput.isPressed(MappedInputManager::Button::PageForward);
  const bool frontPrev = mappedInput.isPressed(MappedInputManager::Button::Left);
  const bool frontNext = mappedInput.isPressed(MappedInputManager::Button::Right);
  const bool fromSideBtn = (sidePrev || sideNext) && !(frontPrev || frontNext);
  const bool nextTriggered = sideNext || frontNext;
  const bool prevTriggered = sidePrev || frontPrev;

  if (!nextTriggered && !prevTriggered) {
    return false;
  }

  const bool skipChapter =
      fromSideBtn ? SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP
                  : SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP;
  const bool specialLongPress =
      fromSideBtn ? SETTINGS.sideButtonLongPress != CrossPointSettings::SIDE_LONG_OFF
                  : SETTINGS.longPressButtonBehavior != CrossPointSettings::LONG_PRESS_OFF;

  if (!specialLongPress) {
    return false;
  }

  turnButtonLongPressHandled = true;
  waitingForConfirmSecondClick = false;
  firstConfirmClickMs = 0UL;

  if (skipChapter) {
    READING_STATS.noteActivity();
    lastPageTurnTime = millis();
    skipToChapter(nextTriggered);
  } else {
    handleTurnButtonLongPress(fromSideBtn);
  }
  return true;
}

void EpubReaderActivity::skipToChapter(const bool forward) {
  if (!epub) {
    return;
  }

  RenderLock lock(*this);
  nextPageNumber = 0;
  pendingPageJump = 0;
  currentSpineIndex = forward ? currentSpineIndex + 1 : currentSpineIndex - 1;
  section.reset();
  lock.unlock();
  requestUpdate();
}

uint8_t EpubReaderActivity::resolveLongPressOrientationTarget() const {
  if (SETTINGS.longPressOrientation == CrossPointSettings::CYCLE_ORIENTATIONS) {
    return static_cast<uint8_t>((SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT);
  }

  const uint8_t target = std::min<uint8_t>(SETTINGS.longPressOrientation, CrossPointSettings::ORIENTATION_COUNT - 1);
  if (SETTINGS.orientation != target) {
    return target;
  }

  return fixedOrientationToggleActive ? fixedOrientationPrevious : CrossPointSettings::PORTRAIT;
}

void EpubReaderActivity::handleTurnButtonLongPress(const bool fromSideBtn) {
  if (fromSideBtn) {
    switch (static_cast<CrossPointSettings::SIDE_LONG_PRESS>(SETTINGS.sideButtonLongPress)) {
      case CrossPointSettings::SIDE_LONG_ORIENTATION_CHANGE: {
        const bool sideRevertingFixedOrientation =
            SETTINGS.longPressOrientation != CrossPointSettings::CYCLE_ORIENTATIONS && fixedOrientationToggleActive &&
            SETTINGS.orientation == SETTINGS.longPressOrientation;
        if (SETTINGS.longPressOrientation != CrossPointSettings::CYCLE_ORIENTATIONS &&
            SETTINGS.orientation != SETTINGS.longPressOrientation) {
          fixedOrientationPrevious = SETTINGS.orientation;
          fixedOrientationToggleActive = true;
        }
        applyOrientation(resolveLongPressOrientationTarget());
        if (sideRevertingFixedOrientation) {
          fixedOrientationToggleActive = false;
        }
        requestUpdate();
        break;
      }
      case CrossPointSettings::SIDE_LONG_CHANGE_FONT_SIZE:
        cycleReaderFontSize();
        break;
      default:
        break;
    }
    return;
  }

  if (SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_ORIENTATION_CHANGE) {
    const bool frontRevertingFixedOrientation =
        SETTINGS.longPressOrientation != CrossPointSettings::CYCLE_ORIENTATIONS && fixedOrientationToggleActive &&
        SETTINGS.orientation == SETTINGS.longPressOrientation;
    if (SETTINGS.longPressOrientation != CrossPointSettings::CYCLE_ORIENTATIONS &&
        SETTINGS.orientation != SETTINGS.longPressOrientation) {
      fixedOrientationPrevious = SETTINGS.orientation;
      fixedOrientationToggleActive = true;
    }
    applyOrientation(resolveLongPressOrientationTarget());
    if (frontRevertingFixedOrientation) {
      fixedOrientationToggleActive = false;
    }
    requestUpdate();
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  // calculates page turn duration by dividing by number of pages
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  // resets cached section so that space is reserved for auto page turn indicator when None or progress bar only
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    // Preserve current reading position so we can restore after reflow.
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  READING_STATS.noteActivity();

  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        section.reset();
      }
    }
  }
  lastPageTurnTime = millis();
  requestUpdate();
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    markStatsCompletedAtEnd(*epub, currentSpineIndex);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  const bool statusBarAtTop = SETTINGS.statusBarPlacement == CrossPointSettings::STATUS_BAR_TOP;

  // reserves space for automatic page turn indicator when no status bar or progress bar only
  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
  } else if (statusBarAtTop) {
    orientedMarginTop += std::max(SETTINGS.screenMargin, statusBarHeight);
  } else {
    orientedMarginBottom += std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                  SETTINGS.imageRendering)) {
      LOG_DBG("ERS", "Cache not found, building...");

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                      SETTINGS.imageRendering, popupFn)) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        section.reset();
        return;
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      if (*pendingPageJump >= section->pageCount && section->pageCount > 0) {
        section->currentPage = section->pageCount - 1;
      } else {
        section->currentPage = *pendingPageJump;
      }
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      } else if (section->currentPage >= section->pageCount && section->pageCount > 0) {
        LOG_DBG("ERS", "Clamping cached page %d to %d", section->currentPage, section->pageCount - 1);
        section->currentPage = section->pageCount - 1;
      }
    }

    if (!pendingAnchor.empty()) {
      if (const auto page = section->getPageForAnchor(pendingAnchor)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
    }

    if (pendingParagraphLookup) {
      if (const auto page = section->getPageForParagraphIndex(pendingParagraphIndex)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved paragraph %u to page %d", pendingParagraphIndex, *page);
      } else {
        LOG_DBG("ERS", "Paragraph %u not found in section %d", pendingParagraphIndex, currentSpineIndex);
      }
      pendingParagraphLookup = false;
    }

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
      requestUpdate();  // Try again after clearing cache
                        // TODO: prevent infinite loop if the page keeps failing to load for some reason
      automaticPageTurnActive = false;
      return;
    }

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  silentIndexNextChapterIfNeeded(viewportWidth, viewportHeight);
  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void EpubReaderActivity::silentIndexNextChapterIfNeeded(const uint16_t viewportWidth, const uint16_t viewportHeight) {
  if (!epub || !section || section->pageCount < 2) {
    return;
  }

  // Build the next chapter cache while the penultimate page is on screen.
  if (section->currentPage != section->pageCount - 2) {
    return;
  }

  const int nextSpineIndex = currentSpineIndex + 1;
  if (nextSpineIndex < 0 || nextSpineIndex >= epub->getSpineItemsCount()) {
    return;
  }

  Section nextSection(epub, nextSpineIndex, renderer);
  if (nextSection.loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                  SETTINGS.imageRendering)) {
    return;
  }

  LOG_DBG("ERS", "Silently indexing next chapter: %d", nextSpineIndex);
  if (!nextSection.createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                     SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                     viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                     SETTINGS.imageRendering)) {
    LOG_ERR("ERS", "Failed silent indexing for chapter: %d", nextSpineIndex);
  }
}

void EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  int progressPercent = 0;
  if (epub->getBookSize() > 0 && pageCount > 0) {
    const float chapterProgress = static_cast<float>(currentPage + 1) / static_cast<float>(pageCount);
    progressPercent =
        clampPercent(static_cast<int>(epub->calculateProgress(spineIndex, chapterProgress) * 100.0f + 0.5f));
  }
  READING_STATS.updateProgress(static_cast<uint8_t>(progressPercent), progressPercent >= 100,
                               getStatsChapterTitle(*epub, spineIndex), getStatsChapterProgressPercent(currentPage, pageCount));

  std::string progressPath = getStableProgressPath(stableBookId);
  if (!progressPath.empty()) {
    BookIdentity::ensureStableDataDir(stableBookId);
  } else {
    progressPath = getLegacyProgressPath(*epub);
  }
  if (writeReaderProgressFile(progressPath, spineIndex, currentPage, pageCount)) {
    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d", spineIndex, currentPage);
  } else {
    LOG_ERR("ERS", "Could not save progress!");
  }
}
void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  const uint32_t heapBefore = esp_get_free_heap_size();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);  // scan pass
  scope.endScanAndPrewarm();
  const uint32_t heapAfter = esp_get_free_heap_size();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

  LOG_DBG("ERS", "Heap: before=%lu after=%lu delta=%ld", heapBefore, heapAfter,
          (int32_t)heapAfter - (int32_t)heapBefore);

  const bool enableTextAA = SETTINGS.textAntiAliasing && !renderer.isDarkMode() &&
                            SETTINGS.textDarkness != CrossPointSettings::TEXT_DARKNESS_LEGACY_BW;
  const bool enableImageGrayscaleOnly = renderer.isDarkMode() && page->hasImages();
  const bool forceFullRefresh = pendingForceFullRefresh;
  pendingForceFullRefresh = false;
  // Force special handling for pages with images when anti-aliasing is on
  bool imagePageWithAA = page->hasImages() && enableTextAA;
  HalDisplay::RefreshMode configuredRefreshMode = HalDisplay::FAST_REFRESH;
  const bool hasConfiguredRefreshMode = ReaderUtils::getConfiguredReaderRefreshMode(configuredRefreshMode);

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
  renderStatusBar();
  fcm->logStats("bw_render");
  const auto tBwRender = millis();

  if (forceFullRefresh) {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, true);
  } else if (hasConfiguredRefreshMode) {
    renderer.displayBuffer(configuredRefreshMode);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    // Double FAST_REFRESH handles ghosting for image pages; don't count toward full refresh cadence
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  const auto tDisplay = millis();

  const bool needsGrayscale = enableTextAA || enableImageGrayscaleOnly;

  // Save bw buffer to reset buffer state after grayscale data sync.
  const bool storedBwBuffer = needsGrayscale && renderer.storeBwBuffer();
  const auto tBwStore = millis();
  if (needsGrayscale && !storedBwBuffer) {
    LOG_ERR("ERS", "Skipping grayscale enhancement: failed to store BW backup");
  }

  // grayscale rendering
  // TODO: Only do this if font supports it
  if (needsGrayscale && storedBwBuffer) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (enableImageGrayscaleOnly) {
      page->renderImages(renderer, orientedMarginLeft, orientedMarginTop);
    } else {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    }
    renderStatusBar();
    renderer.copyGrayscaleLsbBuffers();
    const auto tGrayLsb = millis();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    if (enableImageGrayscaleOnly) {
      page->renderImages(renderer, orientedMarginLeft, orientedMarginTop);
    } else {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    }
    renderStatusBar();
    renderer.copyGrayscaleMsbBuffers();
    const auto tGrayMsb = millis();

    // display grayscale part
    renderer.displayGrayBuffer();
    const auto tGrayDisplay = millis();
    renderer.setRenderMode(GfxRenderer::BW);
    fcm->logStats("gray");

    // restore the bw data
    renderer.restoreBwBuffer();
    const auto tBwRestore = millis();

    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
            "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
            tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
  } else {
    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums grayscale=%s total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay,
            needsGrayscale ? "skipped" : "off", tEnd - t0);
  }
}

void EpubReaderActivity::renderStatusBar() const {
  // Calculate progress in book
  const int currentPage = section->currentPage + 1;
  const float pageCount = section->pageCount;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  std::string title;

  int textYOffset = 0;

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  std::string timeLeftText;
  if (SETTINGS.statusBarTimeLeft != CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_HIDE) {
    const ReadingBookStats* stats = READING_STATS.findMatchingBookForPath(epub->getPath(), epub->getTitle(), epub->getAuthor());
    if (stats) {
      auto estimate = ReadingStatsAnalytics::buildBookTimeLeftEstimate(*stats);
      if (SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_CHAPTER) {
        const auto chapterEstimate = ReadingStatsAnalytics::buildChapterTimeLeftEstimate(*stats);
        if (chapterEstimate.ready &&
            chapterEstimate.confidence != ReadingStatsAnalytics::EstimateConfidence::LOW_CONFIDENCE) {
          estimate = chapterEstimate;
        }
      }
      timeLeftText = ReadingStatsAnalytics::formatCompactTimeLeftEstimate(estimate);
    }
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, timeLeftText);
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->pageCount;
    if (epub && epub->getBookSize() > 0 && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

void EpubReaderActivity::launchKOReaderSync(const SyncLaunchMode mode) {
  if (!epub) {
    return;
  }

  const int currentPage = section ? section->currentPage : 0;
  const int totalPages = section ? section->pageCount : 0;
  KOReaderSyncIntentState syncIntent = KOReaderSyncIntentState::COMPARE;
  if (mode == SyncLaunchMode::PULL_REMOTE) {
    syncIntent = KOReaderSyncIntentState::PULL_REMOTE;
  } else if (mode == SyncLaunchMode::PUSH_LOCAL) {
    syncIntent = KOReaderSyncIntentState::PUSH_LOCAL;
  }

  auto& sync = APP_STATE.koReaderSyncSession;
  sync.active = true;
  sync.epubPath = epub->getPath();
  sync.spineIndex = currentSpineIndex;
  sync.page = currentPage;
  sync.totalPagesInSpine = totalPages;
  if (section) {
    if (const auto pIdx = section->getParagraphIndexForPage(static_cast<uint16_t>(currentPage))) {
      sync.paragraphIndex = *pIdx;
      sync.hasParagraphIndex = true;
      sync.xhtmlSeekHint = 0;
    } else {
      sync.paragraphIndex = 0;
      sync.hasParagraphIndex = false;
      sync.xhtmlSeekHint = 0;
    }
  } else {
    sync.paragraphIndex = 0;
    sync.hasParagraphIndex = false;
    sync.xhtmlSeekHint = 0;
  }
  sync.intent = syncIntent;
  sync.outcome = KOReaderSyncOutcomeState::PENDING;
  sync.resultSpineIndex = 0;
  sync.resultPage = 0;
  sync.resultParagraphIndex = 0;
  sync.resultHasParagraphIndex = false;
  APP_STATE.saveToFile();

  LOG_DBG("ERS", "Standalone sync handoff: spine=%d page=%d/%d", currentSpineIndex, currentPage, totalPages);
  activityManager.goToKOReaderSync();
}

void EpubReaderActivity::applyPendingSyncSession() {
  auto& sync = APP_STATE.koReaderSyncSession;
  if (!sync.active || !epub || sync.epubPath != epub->getPath()) {
    return;
  }

  LOG_DBG("ERS", "Applying pending sync session outcome=%d path=%s", static_cast<int>(sync.outcome),
          sync.epubPath.c_str());

  if (sync.outcome == KOReaderSyncOutcomeState::UPLOAD_COMPLETE) {
    LOG_DBG("ERS", "Upload-complete: keeping existing progress unchanged");
    sync.clear();
    APP_STATE.saveToFile();
    return;
  }

  int restoreSpineIndex = sync.spineIndex;
  int restorePage = sync.page;
  pendingParagraphLookup = sync.hasParagraphIndex;
  pendingParagraphIndex = sync.paragraphIndex;

  if (sync.outcome == KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    restoreSpineIndex = sync.resultSpineIndex;
    restorePage = sync.resultPage;
    pendingParagraphLookup = sync.resultHasParagraphIndex;
    pendingParagraphIndex = sync.resultParagraphIndex;
    LOG_DBG("ERS", "Applying remote position: spine=%d page=%d paragraph=%u", restoreSpineIndex, restorePage,
            pendingParagraphIndex);
  } else {
    LOG_DBG("ERS", "Restoring local pre-sync position: spine=%d page=%d paragraph=%u", restoreSpineIndex, restorePage,
            pendingParagraphIndex);
  }

  const int restorePageCount = (restoreSpineIndex == sync.spineIndex) ? sync.totalPagesInSpine : 0;
  const std::string restoreBookId = BookIdentity::resolveStableBookId(epub->getPath());
  std::string restoreProgressPath = getStableProgressPath(restoreBookId);
  if (!restoreProgressPath.empty()) {
    BookIdentity::ensureStableDataDir(restoreBookId);
  } else {
    restoreProgressPath = getLegacyProgressPath(*epub);
  }

  if (writeReaderProgressFile(restoreProgressPath, restoreSpineIndex, restorePage, restorePageCount)) {
    cachedSpineIndex = restoreSpineIndex;
    cachedChapterTotalPageCount = restorePageCount;
    LOG_DBG("ERS", "Prepared progress.bin for sync restore: spine=%d page=%d/%d", restoreSpineIndex, restorePage,
            sync.totalPagesInSpine);
  } else {
    currentSpineIndex = restoreSpineIndex;
    nextPageNumber = restorePage;
    cachedSpineIndex = restoreSpineIndex;
    cachedChapterTotalPageCount = restorePageCount;
  }

  sync.clear();
  APP_STATE.saveToFile();
}
