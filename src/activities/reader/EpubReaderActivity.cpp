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

#include <iterator>
#include <limits>

#include "AchievementsStore.h"
#include "DictionaryStore.h"
#include "BookmarksActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "QrDisplayActivity.h"
#include "ReaderQuickSettingsActivity.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookIdentity.h"
#include "util/CompletedBookMover.h"
#include "util/ScreenshotUtil.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long bookmarkToggleMs = 700;
// pages per minute, first item is 1 to prevent division by zero if accessed
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
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

  return static_cast<uint8_t>(clampPercent(
      static_cast<int>((static_cast<float>(currentPage + 1) / static_cast<float>(pageCount)) * 100.0f + 0.5f)));
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
  const bool countedSession = READING_STATS.getLastSessionSnapshot().valid &&
                              READING_STATS.getLastSessionSnapshot().counted &&
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

  if (!epub) {
    return;
  }

  ensureSdFontLoaded();

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();
  applyPendingSyncSession();
  stableBookId = BookIdentity::resolveStableBookId(epub->getPath());
  bookmarkStore.load(epub->getCachePath(), stableBookId);

  FsFile f;
  bool loadedProgress = false;
  bool loadedFromLegacy = false;
  const std::string stableProgressPath = getStableProgressPath(stableBookId);
  const std::string legacyProgressPath = getLegacyProgressPath(*epub);
  const std::string progressPath = (!stableProgressPath.empty() && Storage.exists(stableProgressPath.c_str()))
                                       ? stableProgressPath
                                       : legacyProgressPath;
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
      loadedProgress = true;
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
  // Only apply the EPUB text reference on first open; a saved position in spine 0 is valid progress.
  if (!loadedProgress && currentSpineIndex == 0) {
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

  sessionStartSpineIndex = currentSpineIndex;
  sessionStartPage = nextPageNumber;
  sessionProgressTouched = false;

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

  ReaderUtils::requestReaderUiTransitionRefresh(renderer);

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

  // Dictionary mode: power button toggles mode, navigation moves cursor
  if (SETTINGS.shortPwrBtn == CrossPointSettings::DICT_MODE &&
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    dictModeActive = !dictModeActive;
    if (dictModeActive) {
      dictCursorLineIdx = 0;
      dictCursorWordIdx = 0;
      dictDefinition[0] = '\0';
      dictPopupVisible = false;
      dictActiveDictIdx = 0;
      dictPopupScrollOffset = 0;
      // Lazy-initialise: scan and load config the first time dict mode is
      // entered so that dictionaries are available without requiring the user
      // to open the Dictionary Settings screen first.
      if (DICT_STORE.getEntries().empty()) {
        DICT_STORE.scan();
        DICT_STORE.loadConfig();
        // Load checkpoints only for enabled dicts — conserves RAM.
        DICT_STORE.syncCheckpointsToEnabled();
      }
    } else {
      // Exiting dict mode: also cancel any active highlight selection.
      highlightModeActive = false;
    }
    requestUpdate();
    return;
  }

  if (dictModeActive) {
    // Back: cancel highlight mode (if active), else close popup, else exit dict mode
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (highlightModeActive) {
        highlightModeActive = false;
        requestUpdate();
        return;
      }
      if (dictPopupVisible) {
        dictPopupVisible = false;
        dictDefinition[0] = '\0';
      } else {
        dictModeActive = false;
      }
      requestUpdate();
      return;
    }

    // Long-press Confirm: enter highlight/text-selection mode anchored at current cursor
    if (!highlightModeActive && !dictPopupVisible &&
        mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
        mappedInput.getHeldTime() >= bookmarkToggleMs) {
      highlightModeActive = true;
      highlightAnchorLineIdx = dictCursorLineIdx;
      highlightAnchorWordIdx = dictCursorWordIdx;
      requestUpdate();
      return;
    }

    // Short-press Confirm in highlight mode: save selection to MyCLippings and exit highlight mode
    if (highlightModeActive && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      saveHighlightToMyCLippings();
      highlightModeActive = false;
      requestUpdate();
      return;
    }

    // Short-press Confirm (normal dict mode): look up the word under the cursor and show popup
    if (!highlightModeActive && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      dictPopupVisible = true;  // lookup happens in render from page data
      dictActiveDictIdx = 0;
      dictPopupScrollOffset = 0;
      requestUpdate();
      return;
    }

    // When popup is open: Up/Down (= PageBack/PageForward, same physical side buttons)
    // scroll the definition. At the top boundary Up switches to the previous dictionary;
    // at the bottom boundary Down switches to the next dictionary.
    // dictLineNav is used so the proven repeat-timer mechanism is shared with cursor nav.
    if (dictPopupVisible) {
      constexpr int POPUP_VISIBLE_LINES = 4;
      bool popupChanged = false;
      dictLineNav.onPressAndContinuous(
          {MappedInputManager::Button::Up, MappedInputManager::Button::PageBack}, [&] {
            if (dictPopupScrollOffset > 0) {
              // Go back one full page (scrollOffset is always a POPUP_VISIBLE_LINES multiple).
              dictPopupScrollOffset -= POPUP_VISIBLE_LINES;
              popupChanged = true;
            } else if (dictActiveDictIdx > 0 && dictLookedUpWord[0] != '\0') {
              // Backward search: find the nearest previous dict that has a definition.
              int prevIdx = dictActiveDictIdx - 1;
              char testDef[32];
              while (prevIdx >= 0 &&
                     !DICT_STORE.lookupInEnabledEntry(prevIdx, dictLookedUpWord, testDef, sizeof(testDef))) {
                prevIdx--;
              }
              if (prevIdx >= 0) {
                dictActiveDictIdx = prevIdx;
                dictDefinition[0] = '\0';
                dictPopupScrollOffset = 0;
                dictPopupTotalLines = 0;
                popupChanged = true;
              }
            }
          });
      dictLineNav.onPressAndContinuous(
          {MappedInputManager::Button::Down, MappedInputManager::Button::PageForward}, [&] {
            // totalPages = ceil(totalLines / POPUP_VISIBLE_LINES)
            const int totalPages = (dictPopupTotalLines + POPUP_VISIBLE_LINES - 1) / POPUP_VISIBLE_LINES;
            const int currentPage = (POPUP_VISIBLE_LINES > 0)
                                        ? dictPopupScrollOffset / POPUP_VISIBLE_LINES
                                        : 0;
            if (currentPage < totalPages - 1) {
              // Advance one page; scrollOffset stays a POPUP_VISIBLE_LINES multiple.
              dictPopupScrollOffset += POPUP_VISIBLE_LINES;
              popupChanged = true;
            } else if (dictLookedUpWord[0] != '\0') {
              // Forward search: find the nearest next dict that has a definition.
              int nextIdx = dictActiveDictIdx + 1;
              char testDef[32];
              while (nextIdx < DICT_STORE.enabledCount() &&
                     !DICT_STORE.lookupInEnabledEntry(nextIdx, dictLookedUpWord, testDef, sizeof(testDef))) {
                nextIdx++;
              }
              if (nextIdx < DICT_STORE.enabledCount()) {
                dictActiveDictIdx = nextIdx;
                dictDefinition[0] = '\0';
                dictPopupScrollOffset = 0;
                dictPopupTotalLines = 0;
                popupChanged = true;
              }
            }
          });
      if (popupChanged) requestUpdate();
      return;
    }

    // Navigation — uses ButtonNavigator so each axis has its own repeat timer.
    // This prevents cross-axis timer interference (right-hold firing up, etc.).
    bool cursorMoved = false;

    dictLineNav.onPressAndContinuous(
        {MappedInputManager::Button::Up, MappedInputManager::Button::PageBack}, [&] {
          if (dictCursorLineIdx > 0) {
            dictCursorLineIdx--;
            dictCursorWordIdx = 0;
            dictPopupVisible = false;
            dictDefinition[0] = '\0';
            cursorMoved = true;
          }
        });

    dictLineNav.onPressAndContinuous(
        {MappedInputManager::Button::Down, MappedInputManager::Button::PageForward}, [&] {
          dictCursorLineIdx++;
          dictCursorWordIdx = 0;
          dictPopupVisible = false;
          dictDefinition[0] = '\0';
          cursorMoved = true;
        });

    dictWordNav.onPressAndContinuous({MappedInputManager::Button::Left}, [&] {
      if (dictCursorWordIdx > 0) {
        dictCursorWordIdx--;
      } else if (dictCursorLineIdx > 0) {
        dictCursorLineIdx--;
        dictCursorWordIdx = 999;  // will be clamped on render
      }
      dictPopupVisible = false;
      dictDefinition[0] = '\0';
      cursorMoved = true;
    });

    dictWordNav.onPressAndContinuous({MappedInputManager::Button::Right}, [&] {
      if (dictCursorWordIdx + 1 < dictCurrentLineWordCount) {
        dictCursorWordIdx++;
      } else {
        // end of line: wrap to first word of next line
        dictCursorLineIdx++;
        dictCursorWordIdx = 0;
      }
      dictPopupVisible = false;
      dictDefinition[0] = '\0';
      cursorMoved = true;
    });

    if (cursorMoved) {
      requestUpdate(true);  // immediate: wake render task without waiting for loop end
    }
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= bookmarkToggleMs) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
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
    ReaderUtils::requestReaderUiTransitionRefresh(renderer);
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

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    const std::string fileBrowserPath = moveCompletedBookIfEnabled();
    READING_STATS.endSession();
    ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
    showPendingAchievementPopups(renderer);
    activityManager.goToFileBrowser(fileBrowserPath);
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    if (tryAutoPushOnClose()) {
      return;
    }
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

  // At end of the book, forward button goes home and back button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (nextTriggered) {
      if (tryAutoPushOnClose()) {
        return;
      }
      exitReaderAfterOptionalCompletedMove();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const bool longPress = !fromTilt && mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;

  // Don't skip chapter after screenshot
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP) {
    READING_STATS.noteActivity();
    lastPageTurnTime = millis();
    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      if (!nextTriggered) {
        pendingPageJump = std::numeric_limits<uint16_t>::max();
      }
      currentSpineIndex = nextTriggered ? currentSpineIndex + 1 : currentSpineIndex - 1;
      sessionProgressTouched = true;
      section.reset();
    }
    requestUpdate();
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_ORIENTATION_CHANGE) {
    const uint8_t newOrientation = nextTriggered ? (SETTINGS.orientation - 1 + CrossPointSettings::ORIENTATION_COUNT) %
                                                       CrossPointSettings::ORIENTATION_COUNT
                                                 : (SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestUpdate();
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
    sessionProgressTouched = true;
    section.reset();
  }
}

EpubReaderActivity::ReaderSettingsSnapshot EpubReaderActivity::captureReaderSettingsSnapshot() const {
  return ReaderSettingsSnapshot{
      SETTINGS.darkMode,
      SETTINGS.fadingFix,
      SETTINGS.refreshFrequency,
      SETTINGS.fontFamily,
      SETTINGS.fontSize,
      SETTINGS.lineSpacing,
      SETTINGS.screenMargin,
      SETTINGS.paragraphAlignment,
      SETTINGS.embeddedStyle,
      SETTINGS.hyphenationEnabled,
      SETTINGS.bionicReading,
      SETTINGS.orientation,
      SETTINGS.extraParagraphSpacing,
      SETTINGS.forceParagraphIndents,
      SETTINGS.textAntiAliasing,
      SETTINGS.textDarkness,
      SETTINGS.readerRefreshMode,
      SETTINGS.imageRendering,
      SETTINGS.sdFontFamilyName,
  };
}

void EpubReaderActivity::applyReaderSettingsChanges(const ReaderSettingsSnapshot& before) {
  const bool fontChanged = before.fontFamily != SETTINGS.fontFamily || before.fontSize != SETTINGS.fontSize ||
                           before.sdFontFamilyName != SETTINGS.sdFontFamilyName;
  const bool paginationChanged =
      fontChanged || before.lineSpacing != SETTINGS.lineSpacing || before.screenMargin != SETTINGS.screenMargin ||
      before.paragraphAlignment != SETTINGS.paragraphAlignment || before.embeddedStyle != SETTINGS.embeddedStyle ||
      before.hyphenationEnabled != SETTINGS.hyphenationEnabled ||
      before.extraParagraphSpacing != SETTINGS.extraParagraphSpacing ||
      before.forceParagraphIndents != SETTINGS.forceParagraphIndents ||
      before.imageRendering != SETTINGS.imageRendering;
  const bool orientationChanged = before.orientation != SETTINGS.orientation;
  const bool refreshPolicyChanged =
      before.refreshFrequency != SETTINGS.refreshFrequency || before.readerRefreshMode != SETTINGS.readerRefreshMode;
  const bool renderOnlyChanged = before.darkMode != SETTINGS.darkMode || before.fadingFix != SETTINGS.fadingFix ||
                                 before.bionicReading != SETTINGS.bionicReading ||
                                 before.textAntiAliasing != SETTINGS.textAntiAliasing ||
                                 before.textDarkness != SETTINGS.textDarkness || refreshPolicyChanged;
  const bool displayModeChanged = before.darkMode != SETTINGS.darkMode;
  const bool needsFullRefresh = orientationChanged || paginationChanged || displayModeChanged;

  if (!(paginationChanged || orientationChanged || renderOnlyChanged)) {
    return;
  }

  if (fontChanged) {
    ensureSdFontLoaded();
  }

  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setDarkMode(SETTINGS.darkMode);
  renderer.setTextDarkness(SETTINGS.textDarkness);
  if (needsFullRefresh) {
    renderer.requestNextFullRefresh();
  }

  if (orientationChanged || paginationChanged) {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    if (orientationChanged) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    section.reset();
  }

  if (refreshPolicyChanged) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  }

  pendingForceFullRefresh = needsFullRefresh;
  requestUpdate(true);
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::READER_SETTINGS: {
      const auto before = captureReaderSettingsSnapshot();
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<ReaderQuickSettingsActivity>(renderer, mappedInput),
                             [this, before](const ActivityResult&) {
                               applyReaderSettingsChanges(before);
                               READING_STATS.resumeSession();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              const auto& chapterResult = std::get<ChapterResult>(result.data);
              RenderLock lock(*this);

              currentSpineIndex = chapterResult.spineIndex;

              // If anchor is not empty, it will be used later to calculate the page number.
              pendingAnchor = chapterResult.anchor;

              // Otherwise page 0 will be used.
              nextPageNumber = 0;
              sessionProgressTouched = true;
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
      startActivityForResult(std::make_unique<BookmarksActivity>(
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
                                   sessionProgressTouched = true;
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
      if (tryAutoPushOnClose()) {
        return;
      }
      exitReaderAfterOptionalCompletedMove();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::MARK_AS_FINISHED: {
      const std::string title = epub ? epub->getTitle() : "";
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_MARK_AS_FINISHED_CONFIRM), title),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              markCurrentBookAsFinished();
            } else {
              requestUpdate();
            }
          });
      break;
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
      exitReaderAfterOptionalCompletedMove();
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

bool EpubReaderActivity::tryAutoPushOnClose() {
  if (!SETTINGS.koSyncAutoPushOnClose || !KOREADER_STORE.hasCredentials() || !epub || !section) {
    return false;
  }

  const int currentPage = section->currentPage;
  const bool positionChanged =
      sessionProgressTouched || currentSpineIndex != sessionStartSpineIndex || currentPage != sessionStartPage;
  if (!positionChanged) {
    return false;
  }

  LOG_DBG("ERS", "Auto-push KOReader sync before closing: spine=%d page=%d", currentSpineIndex, currentPage);
  launchKOReaderSync(SyncLaunchMode::AUTO_PUSH);
  return true;
}

std::string EpubReaderActivity::moveCompletedBookIfEnabled() {
  if (!epub) {
    return "";
  }

  const std::string sourcePath = epub->getPath();
  if (!SETTINGS.moveCompletedBooks) {
    return sourcePath;
  }

  const auto* statsBook = READING_STATS.findBook(!stableBookId.empty() ? stableBookId : sourcePath);
  if (!statsBook || !statsBook->completed) {
    return sourcePath;
  }

  const std::string title = epub->getTitle();
  const std::string author = epub->getAuthor();
  const std::string coverBmpPath = epub->getCoverBmpPath();
  {
    RenderLock lock(*this);
    section.reset();
    epub.reset();
  }

  const auto moveResult =
      CompletedBookMover::moveCompletedBookIfEnabled(sourcePath, title, author, coverBmpPath, stableBookId);
  return moveResult.moved ? moveResult.destinationPath : sourcePath;
}

void EpubReaderActivity::exitReaderAfterOptionalCompletedMove() {
  const std::string exitPath = moveCompletedBookIfEnabled();
  exitReaderToHomeOrStats(renderer, mappedInput, exitPath);
}

void EpubReaderActivity::markCurrentBookAsFinished() {
  if (!epub) {
    activityManager.goHome();
    return;
  }

  READING_STATS.noteActivity();
  markStatsCompletedAtEnd(*epub, currentSpineIndex);
  exitReaderAfterOptionalCompletedMove();
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  READING_STATS.noteActivity();
  const int oldSpineIndex = currentSpineIndex;
  const int oldPage = section ? section->currentPage : nextPageNumber;

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
  const int newPage = section ? section->currentPage : nextPageNumber;
  if (currentSpineIndex != oldSpineIndex || newPage != oldPage) {
    sessionProgressTouched = true;
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

  // reserves space for automatic page turn indicator when no status bar or progress bar only
  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
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
                                  SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                  SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                  SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering)) {
      LOG_DBG("ERS", "Cache not found, building...");

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(
              SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
              SETTINGS.forceParagraphIndents, SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
              SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering, popupFn)) {
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

    bool resolvedSyncLut = false;
    if (pendingListItemLookup) {
      if (const auto page = section->getPageForListItemIndex(pendingListItemIndex)) {
        section->currentPage = *page;
        resolvedSyncLut = true;
        LOG_DBG("ERS", "Resolved list item %u to page %d", pendingListItemIndex, *page);
      } else {
        LOG_DBG("ERS", "List item %u not found in section %d", pendingListItemIndex, currentSpineIndex);
      }
      pendingListItemLookup = false;
    }

    if (!resolvedSyncLut && pendingParagraphLookup) {
      if (const auto page = section->getPageForParagraphIndex(pendingParagraphIndex)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved paragraph %u to page %d", pendingParagraphIndex, *page);
      } else {
        LOG_DBG("ERS", "Paragraph %u not found in section %d", pendingParagraphIndex, currentSpineIndex);
      }
    }
    pendingParagraphLookup = false;

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

    // Dict mode: extract cursor word position before page is consumed
    struct DictCursorInfo {
      bool valid = false;
      int16_t x = 0;
      int16_t y = 0;
      int16_t lineH = 0;
      int16_t wordW = 0;        // pixel width of displayWord (for highlight rect)
      std::string word;         // alpha-stripped / hyphen-stitched word for lookup
      std::string displayWord;  // raw token as rendered on screen (for white re-draw)
    } dictCursor;

    // Highlight mode: word positions for the selected range, collected before page is consumed.
    // Stack-allocated — only populated when highlightModeActive, zero cost otherwise.
    struct HlWord {
      int16_t x, y, w;
      char text[20];  // raw display token, null-terminated, truncated to 19 chars
    };
    constexpr int MAX_HL_WORDS = 96;
    HlWord hlWords[MAX_HL_WORDS];
    int hlWordCount = 0;

    if (dictModeActive) {
      // First pass: count all PageLine elements to get total for clamping.
      int totalLines = 0;
      for (const auto& el : p->elements) {
        if (el->getTag() == TAG_PageLine) totalLines++;
      }
      // Clamp BEFORE searching so we always find a valid entry.
      if (totalLines > 0 && dictCursorLineIdx >= totalLines) {
        dictCursorLineIdx = totalLines - 1;
      }

      // Second pass: find the target line.
      // Also collect context words from adjacent lines for hyphenation detection:
      //   prevLineLastRaw – last stored word of line (dictCursorLineIdx-1)
      //   nextLineFirstRaw – first stored word of line (dictCursorLineIdx+1)
      // When the hyphenator split a word at a line break it appends a literal '-'
      // to the prefix entry and places the remainder as the first word of the next
      // line.  We detect both halves so that a lookup on either half finds the
      // full word.
      int lineIdx = 0;
      std::string rawWord;
      std::string prevLineLastRaw;
      std::string nextLineFirstRaw;
      bool foundCursorLine = false;

      for (const auto& el : p->elements) {
        if (el->getTag() != TAG_PageLine) continue;
        const auto* pl = static_cast<const PageLine*>(el.get());
        const auto& elWords = pl->getBlock()->getWords();

        if (lineIdx == dictCursorLineIdx - 1) {
          if (!elWords.empty()) prevLineLastRaw = elWords.back();
        }

        if (lineIdx == dictCursorLineIdx) {
          foundCursorLine = true;
          if (!elWords.empty()) {
            const auto& xpos = pl->getBlock()->getWordXpos();
            dictCurrentLineWordCount = static_cast<int>(elWords.size());
            if (dictCursorWordIdx >= dictCurrentLineWordCount) {
              dictCursorWordIdx = dictCurrentLineWordCount - 1;
            }
            dictCursor.valid = true;
            dictCursor.x = pl->xPos + orientedMarginLeft + xpos[dictCursorWordIdx];
            dictCursor.y = pl->yPos + orientedMarginTop;
            rawWord = elWords[dictCursorWordIdx];
            dictCursor.displayWord = rawWord;
            const int readerFontId = SETTINGS.getReaderFontId();
            dictCursor.lineH = static_cast<int16_t>(renderer.getLineHeight(readerFontId));
            dictCursor.wordW  = static_cast<int16_t>(renderer.getTextWidth(readerFontId, rawWord.c_str()));
            // Strip leading/trailing punctuation so the lookup word is clean.
            size_t ws = 0;
            while (ws < rawWord.size() && !isalpha(static_cast<unsigned char>(rawWord[ws]))) ws++;
            size_t we = rawWord.size();
            while (we > ws && !isalpha(static_cast<unsigned char>(rawWord[we - 1]))) we--;
            dictCursor.word = rawWord.substr(ws, we - ws);
          }
        }

        if (foundCursorLine && lineIdx == dictCursorLineIdx + 1) {
          if (!elWords.empty()) nextLineFirstRaw = elWords.front();
          break;
        }

        lineIdx++;
      }

      // Hyphenation detection: the hyphenator stores "ridic-" on line N and
      // "ulous" on line N+1.  We stitch both halves so the full word is looked up.
      if (!dictCursor.word.empty()) {
        // Helper: extract the alpha-only core of a raw word token.
        auto alphaCore = [](const std::string& w) -> std::string {
          size_t s = 0;
          while (s < w.size() && !isalpha(static_cast<unsigned char>(w[s]))) s++;
          size_t e = w.size();
          while (e > s && !isalpha(static_cast<unsigned char>(w[e - 1]))) e--;
          return w.substr(s, e - s);
        };

        // Case 1: selected word's raw form ends with '-' → cursor is on the
        //         first half; append the alpha core of the next line's first word.
        if (!rawWord.empty() && rawWord.back() == '-' && !nextLineFirstRaw.empty()) {
          dictCursor.word += alphaCore(nextLineFirstRaw);
        }
        // Case 2: cursor is on the first word of the line AND the previous
        //         line's last word ends with '-' → cursor is on the second half;
        //         prepend the alpha core (without the trailing '-') of that word.
        else if (dictCursorWordIdx == 0 && !prevLineLastRaw.empty() &&
                 prevLineLastRaw.back() == '-') {
          dictCursor.word = alphaCore(prevLineLastRaw) + dictCursor.word;
        }
      }

      // Perform dictionary lookup only when Confirm was pressed (dictPopupVisible set in loop).
      // Try dicts starting from dictActiveDictIdx; auto-advance to the first dict that has a hit
      // (restores the original fallback behaviour while still allowing manual dict switching).
      if (dictCursor.valid && dictPopupVisible && dictDefinition[0] == '\0') {
        const int numDicts = DICT_STORE.enabledCount();
        bool found = false;
        for (int di = dictActiveDictIdx; di < numDicts; di++) {
          if (DICT_STORE.lookupInEnabledEntry(di, dictCursor.word.c_str(),
                                              dictDefinition, sizeof(dictDefinition))) {
            dictActiveDictIdx = di;
            // Save the word so loop() can search backward across dicts.
            strncpy(dictLookedUpWord, dictCursor.word.c_str(), sizeof(dictLookedUpWord) - 1);
            dictLookedUpWord[sizeof(dictLookedUpWord) - 1] = '\0';
            found = true;
            break;
          }
        }
        if (!found) {
          strncpy(dictDefinition, tr(STR_NO_DEFINITION), sizeof(dictDefinition) - 1);
          dictDefinition[sizeof(dictDefinition) - 1] = '\0';
        }
      }

      // Highlight mode: collect word positions for the selection range.
      if (highlightModeActive) {
        // Clamp anchor in case the page was re-rendered with fewer lines.
        if (totalLines > 0 && highlightAnchorLineIdx >= totalLines) {
          highlightAnchorLineIdx = totalLines - 1;
        }

        int selStartLine = highlightAnchorLineIdx;
        int selStartWord = highlightAnchorWordIdx;
        int selEndLine = dictCursorLineIdx;
        int selEndWord = dictCursorWordIdx;

        // Normalise: ensure start <= end positionally.
        if (selEndLine < selStartLine ||
            (selEndLine == selStartLine && selEndWord < selStartWord)) {
          std::swap(selStartLine, selEndLine);
          std::swap(selStartWord, selEndWord);
        }

        const int hlFontId = SETTINGS.getReaderFontId();
        int li = 0;
        for (const auto& el : p->elements) {
          if (el->getTag() != TAG_PageLine) continue;
          if (li > selEndLine) break;
          if (li >= selStartLine) {
            const auto* pl = static_cast<const PageLine*>(el.get());
            if (!pl->getBlock()) { li++; continue; }
            const auto& hlElWords = pl->getBlock()->getWords();
            const auto& hlXpos   = pl->getBlock()->getWordXpos();
            const int numWords = static_cast<int>(hlElWords.size());
            if (numWords == 0) { li++; continue; }

            const int wFirst = (li == selStartLine) ? std::min(selStartWord, numWords - 1) : 0;
            const int wLast  = (li == selEndLine)   ? std::min(selEndWord,   numWords - 1) : numWords - 1;

            for (int wi = wFirst; wi <= wLast && hlWordCount < MAX_HL_WORDS; wi++) {
              HlWord& hw = hlWords[hlWordCount++];
              hw.x = static_cast<int16_t>(pl->xPos + orientedMarginLeft + hlXpos[wi]);
              hw.y = static_cast<int16_t>(pl->yPos + orientedMarginTop);
              const auto& rawW = hlElWords[wi];
              const int len = std::min(static_cast<int>(rawW.size()),
                                       static_cast<int>(sizeof(hw.text)) - 1);
              memcpy(hw.text, rawW.c_str(), len);
              hw.text[len] = '\0';
              hw.w = static_cast<int16_t>(renderer.getTextWidth(hlFontId, hw.text));
            }
          }
          li++;
        }
      }
    }

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);

    // Dict mode overlay: draw cursor or highlight range; draw popup only when visible
    if (dictModeActive && dictCursor.valid) {
      const int screenW = renderer.getScreenWidth();
      const int screenH = renderer.getScreenHeight();
      const int readerFontId = SETTINGS.getReaderFontId();

      if (highlightModeActive && hlWordCount > 0) {
        // Highlight range: invert every selected word (black bg, white text).
        const int lineH = static_cast<int>(renderer.getLineHeight(readerFontId));
        for (int i = 0; i < hlWordCount; i++) {
          renderer.fillRect(hlWords[i].x, hlWords[i].y, hlWords[i].w, lineH, true);
          renderer.drawText(readerFontId, hlWords[i].x, hlWords[i].y, hlWords[i].text, false);
        }
      } else if (!highlightModeActive) {
        // Single cursor word inversion (normal dict mode).
        renderer.fillRect(dictCursor.x, dictCursor.y, dictCursor.wordW, dictCursor.lineH, true);
        renderer.drawText(readerFontId, dictCursor.x, dictCursor.y,
                          dictCursor.displayWord.c_str(), false);
      }

      // Draw definition popup only after Confirm was pressed (not available in highlight mode)
      if (!highlightModeActive && dictPopupVisible) {
        constexpr int POPUP_PAD = 8;
        constexpr int POPUP_MAX_LINES = 4;
        const int lineH = renderer.getLineHeight(UI_12_FONT_ID) + 2;
        const int popupH = POPUP_PAD * 2 + lineH * POPUP_MAX_LINES;
        const int popupY = (dictCursor.y < screenH / 2) ? (screenH - popupH - 4) : 4;
        renderer.fillRect(0, popupY, screenW, popupH, false);
        renderer.drawRect(2, popupY + 2, screenW - 4, popupH - 4, true);
        const char* defText = (dictDefinition[0] != '\0') ? dictDefinition : tr(STR_NO_DEFINITION);
        // Wrap to a generous limit, then show a scrollable window of POPUP_MAX_LINES.
        constexpr int POPUP_WRAP_MAX = 20;
        const auto allLines = renderer.wrappedText(UI_12_FONT_ID, defText,
                                                   screenW - POPUP_PAD * 2, POPUP_WRAP_MAX);
        const int totalDefLines = static_cast<int>(allLines.size());
        dictPopupTotalLines = totalDefLines;  // expose to loop() for boundary dict-switching
        // Clamp scroll offset to a valid page boundary (always a multiple of POPUP_MAX_LINES).
        // This handles the case where the definition changed (e.g. different dict) and the
        // previous offset is past the last page.
        if (totalDefLines > 0) {
          const int lastPageStart = ((totalDefLines - 1) / POPUP_MAX_LINES) * POPUP_MAX_LINES;
          if (dictPopupScrollOffset > lastPageStart) {
            dictPopupScrollOffset = lastPageStart;
          }
        } else {
          dictPopupScrollOffset = 0;
        }
        int lineY = popupY + POPUP_PAD;
        const int startLine = dictPopupScrollOffset;
        const int endLine = std::min(startLine + POPUP_MAX_LINES, totalDefLines);
        for (int i = startLine; i < endLine; i++) {
          renderer.drawText(UI_12_FONT_ID, POPUP_PAD, lineY, allLines[i].c_str(), true);
          lineY += lineH;
        }
      }
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
  }
  silentIndexNextChapterIfNeeded(viewportWidth, viewportHeight);
  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void EpubReaderActivity::saveHighlightToMyCLippings() {
  if (!section || !epub) return;

  auto p = section->loadPageFromSectionFile();
  if (!p) return;

  // Normalise selection bounds.
  int selStartLine = highlightAnchorLineIdx;
  int selStartWord = highlightAnchorWordIdx;
  int selEndLine   = dictCursorLineIdx;
  int selEndWord   = dictCursorWordIdx;
  if (selEndLine < selStartLine ||
      (selEndLine == selStartLine && selEndWord < selStartWord)) {
    std::swap(selStartLine, selEndLine);
    std::swap(selStartWord, selEndWord);
  }

  // Walk the page and concatenate selected words into a plain-text string.
  std::string selectedText;
  selectedText.reserve(256);
  int li = 0;
  bool prevHyphenated = false;
  for (const auto& el : p->elements) {
    if (el->getTag() != TAG_PageLine) continue;
    if (li > selEndLine) break;
    if (li >= selStartLine) {
      const auto* pl = static_cast<const PageLine*>(el.get());
      if (!pl->getBlock()) { li++; continue; }
      const auto& words = pl->getBlock()->getWords();
      const int numWords = static_cast<int>(words.size());
      if (numWords == 0) { li++; continue; }

      const int wFirst = (li == selStartLine) ? std::min(selStartWord, numWords - 1) : 0;
      const int wLast  = (li == selEndLine)   ? std::min(selEndWord,   numWords - 1) : numWords - 1;

      for (int wi = wFirst; wi <= wLast; wi++) {
        const std::string& w = words[wi];
        if (!selectedText.empty() && !prevHyphenated) selectedText += ' ';
        prevHyphenated = false;
        // Stitch hyphenated line breaks: last word on a non-final line ending with '-'.
        if (wi == numWords - 1 && li < selEndLine && !w.empty() && w.back() == '-') {
          selectedText += w.substr(0, w.size() - 1);
          prevHyphenated = true;
        } else {
          selectedText += w;
        }
      }
    }
    li++;
  }

  if (selectedText.empty()) return;

  // Compute book progress percentage.
  float bookProgress = 0.0f;
  if (epub->getBookSize() > 0 && section->pageCount > 0) {
    const float chapterProgress =
        static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  const int progressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));

  // Append a Kindle-style clipping entry to /MyClippings.txt.
  FsFile file = Storage.open("/MyClippings.txt", O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("ERS", "saveHighlight: failed to open /MyClippings.txt");
    return;
  }
  const std::string entry = std::string("==========\n") +
                            epub->getTitle() + " (" + epub->getAuthor() + ")\n" +
                            "- Your Highlight | Location: " + std::to_string(progressPercent) + "%\n\n" +
                            selectedText + "\n\n";
  file.write(entry.c_str(), entry.size());
  file.close();

  LOG_DBG("ERS", "Highlight saved: %d chars at %d%%", static_cast<int>(selectedText.size()), progressPercent);

  // Brief confirmation popup.
  GUI.drawPopup(renderer, tr(STR_HIGHLIGHT_SAVED));
  renderer.displayBuffer();
  delay(500);
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
                                  SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                  SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                  SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering)) {
    return;
  }

  LOG_DBG("ERS", "Silently indexing next chapter: %d", nextSpineIndex);
  if (!nextSection.createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                     SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                     SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                     SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering)) {
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
                               getStatsChapterTitle(*epub, spineIndex),
                               getStatsChapterProgressPercent(currentPage, pageCount));

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
  page->recordFontUsage(*fcm, SETTINGS.getReaderFontId());
  scope.endScanAndPrewarm();
  const uint32_t heapAfter = esp_get_free_heap_size();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

  LOG_DBG("ERS", "Heap: before=%lu after=%lu delta=%ld", heapBefore, heapAfter,
          (int32_t)heapAfter - (int32_t)heapBefore);

  const bool enableTextAA = SETTINGS.textAntiAliasing && !renderer.isDarkMode();
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

  // Save BW buffer to reset framebuffer and controller state after grayscale data sync.
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
    LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums grayscale=%s total=%lums",
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

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset);
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
  } else if (mode == SyncLaunchMode::AUTO_PUSH) {
    syncIntent = KOReaderSyncIntentState::AUTO_PUSH;
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
      if (const auto hint = section->getXhtmlByteOffsetForPage(static_cast<uint16_t>(currentPage))) {
        sync.xhtmlSeekHint = *hint;
      } else {
        sync.xhtmlSeekHint = 0;
      }
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
  sync.resultListItemIndex = 0;
  sync.resultHasListItemIndex = false;
  sync.exitToHomeAfterSync = mode == SyncLaunchMode::AUTO_PUSH;
  sync.autoPullEpubPath.clear();
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

  if (sync.intent == KOReaderSyncIntentState::AUTO_PULL && sync.outcome != KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    LOG_DBG("ERS", "Auto-pull finished without a remote position; keeping local progress");
    sync.clear();
    APP_STATE.saveToFile();
    return;
  }

  int restoreSpineIndex = sync.spineIndex;
  int restorePage = sync.page;
  pendingParagraphLookup = sync.hasParagraphIndex;
  pendingParagraphIndex = sync.paragraphIndex;
  pendingListItemLookup = false;
  pendingListItemIndex = 0;

  if (sync.outcome == KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    restoreSpineIndex = sync.resultSpineIndex;
    restorePage = sync.resultPage;
    pendingParagraphLookup = sync.resultHasParagraphIndex;
    pendingParagraphIndex = sync.resultParagraphIndex;
    pendingListItemLookup = sync.resultHasListItemIndex;
    pendingListItemIndex = sync.resultListItemIndex;
    LOG_DBG("ERS", "Applying remote position: spine=%d page=%d paragraph=%u listItem=%u", restoreSpineIndex,
            restorePage, pendingParagraphIndex, pendingListItemIndex);
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
