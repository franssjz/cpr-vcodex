#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadMeActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"

namespace {
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
constexpr int HOME_SHORTCUT_PAGE_SIZE = 4;

struct HomeShortcutEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
  bool isOpds = false;
};

void drawHomeDate(const GfxRenderer& renderer, const ThemeMetrics& metrics, const int pageWidth,
                  const std::string& dateText) {
  if (dateText.empty()) {
    return;
  }

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = pageWidth - 12 - metrics.batteryWidth;
  int rightEdge = batteryX - 8;

  if (showBatteryPercentage) {
    const std::string batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
    rightEdge -= renderer.getTextWidth(SMALL_FONT_ID, batteryText.c_str()) + 4;
  }

  const int dateWidth = renderer.getTextWidth(SMALL_FONT_ID, dateText.c_str());
  const int dateX = std::max(metrics.contentSidePadding, rightEdge - dateWidth);
  renderer.drawText(SMALL_FONT_ID, dateX, metrics.topPadding + 5, dateText.c_str());
}

std::string formatDurationHmCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string getReadingStatsShortcutSubtitle() {
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string todayValue = formatDurationHmCompact(todayReadingMs);
  const std::string goalValue = formatDurationHmCompact(getDailyReadingGoalMs());
  return todayValue + " / " + goalValue + " | " + std::to_string(READING_STATS.getCurrentStreakDays());
}

std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}

std::vector<HomeShortcutEntry> getHomeShortcutEntries(const bool hasOpdsUrl) {
  std::vector<HomeShortcutEntry> entries;
  entries.push_back(HomeShortcutEntry{nullptr, true, false});

  for (const auto& definition : getShortcutDefinitions()) {
    const auto location = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr));
    if (location == CrossPointSettings::SHORTCUT_HOME && getShortcutVisibility(definition)) {
      entries.push_back(HomeShortcutEntry{&definition});
    }
  }

  std::stable_sort(entries.begin(), entries.end(), [](const HomeShortcutEntry& lhs, const HomeShortcutEntry& rhs) {
    const uint8_t lhsOrder = lhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*lhs.definition);
    const uint8_t rhsOrder = rhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*rhs.definition);
    return lhsOrder < rhsOrder;
  });

  if (hasOpdsUrl) {
    entries.push_back(HomeShortcutEntry{nullptr, false, true});
  }

  return entries;
}

std::string getHomeShortcutTitle(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return tr(STR_APPS);
  }
  if (entry.isOpds) {
    return tr(STR_OPDS_BROWSER);
  }
  if (!entry.definition) {
    return "";
  }
  return I18N.get(entry.definition->nameId);
}

std::string getHomeShortcutSubtitle(const HomeShortcutEntry& entry) {
  return (entry.definition && entry.definition->id == ShortcutId::Stats) ? getReadingStatsShortcutSubtitle() : "";
}

UIIcon getHomeShortcutIcon(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return UIIcon::Book;
  }
  if (entry.isOpds) {
    return UIIcon::Library;
  }
  return entry.definition ? entry.definition->icon : UIIcon::Folder;
}

bool showHomeShortcutAccessory(const HomeShortcutEntry& entry) {
  return entry.definition && entry.definition->id == ShortcutId::Stats &&
         READING_STATS.getTodayReadingMs() >= getDailyReadingGoalMs();
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  return static_cast<int>(recentBooks.size() + getHomeShortcutEntries(hasOpdsUrl).size());
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;
  bool needsRefresh = false;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          needsRefresh = true;
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            needsRefresh = true;
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
  if (needsRefresh) {
    requestUpdate();
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto homeEntries = getHomeShortcutEntries(hasOpdsUrl);

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < recentBooks.size()) {
      if (mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
        const RecentBook selectedBook = recentBooks[selectorIndex];
        const int currentSelection = selectorIndex;
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_RECENTS),
                                                   getRecentBookConfirmationLabel(selectedBook)),
            [this, selectedBook, currentSelection](const ActivityResult& result) {
              if (result.isCancelled) {
                requestUpdate();
                return;
              }

              if (RECENT_BOOKS.removeBook(selectedBook.path)) {
                const auto& metrics = UITheme::getInstance().getMetrics();
                loadRecentBooks(metrics.homeRecentBooksCount);
                if (recentBooks.empty()) {
                  selectorIndex = 0;
                } else if (currentSelection >= static_cast<int>(recentBooks.size())) {
                  selectorIndex = static_cast<int>(recentBooks.size()) - 1;
                } else {
                  selectorIndex = currentSelection;
                }
                coverRendered = false;
                freeCoverBuffer();
              }
              requestUpdate(true);
            });
        return;
      }

      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    const int homeIndex = selectorIndex - static_cast<int>(recentBooks.size());
    if (homeIndex < 0 || homeIndex >= static_cast<int>(homeEntries.size())) {
      return;
    }

    const auto& selectedEntry = homeEntries[homeIndex];
    if (selectedEntry.isAppsHub) {
      onAppsOpen();
    } else if (selectedEntry.isOpds) {
      onOpdsBrowserOpen();
    } else if (selectedEntry.definition) {
      switch (selectedEntry.definition->id) {
        case ShortcutId::BrowseFiles:
          onFileBrowserOpen();
          break;
        case ShortcutId::Stats:
        case ShortcutId::ReadingStats:
          onReadingStatsOpen();
          break;
        case ShortcutId::SyncDay:
          onSyncDayOpen();
          break;
        case ShortcutId::Settings:
          activityManager.goToSettings();
          break;
        case ShortcutId::ReadingHeatmap:
          startActivityForResult(std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
        case ShortcutId::Achievements:
          startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
        case ShortcutId::IfFound:
          startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
        case ShortcutId::ReadMe:
          startActivityForResult(std::make_unique<ReadMeActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
        case ShortcutId::RecentBooks:
          activityManager.goToRecentBooks();
          break;
        case ShortcutId::Bookmarks:
          startActivityForResult(std::make_unique<BookmarksAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
        case ShortcutId::FileTransfer:
          activityManager.goToFileTransfer();
          break;
        case ShortcutId::Sleep:
          startActivityForResult(std::make_unique<SleepAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
      }
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  const std::string homeDate = HeaderDateUtils::getDisplayDateText();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
  drawHomeDate(renderer, metrics, pageWidth, homeDate);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  const auto homeEntries = getHomeShortcutEntries(hasOpdsUrl);
  const int selectedHomeIndex = selectorIndex - static_cast<int>(recentBooks.size());
  const Rect shortcutsRect{
      0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
      pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing +
                    metrics.buttonHintsHeight + metrics.verticalSpacing)};

  if (static_cast<int>(homeEntries.size()) <= HOME_SHORTCUT_PAGE_SIZE) {
    GUI.drawButtonMenu(
        renderer, shortcutsRect, static_cast<int>(homeEntries.size()), selectedHomeIndex,
        [&homeEntries](const int index) { return getHomeShortcutTitle(homeEntries[index]); },
        [&homeEntries](const int index) { return getHomeShortcutIcon(homeEntries[index]); },
        [&homeEntries](const int index) { return getHomeShortcutSubtitle(homeEntries[index]); },
        [&homeEntries](const int index) { return showHomeShortcutAccessory(homeEntries[index]); });
  } else {
    const int headerHeight = 34;
    const int listTop = shortcutsRect.y + headerHeight + 12;
    const int listHeight = std::max(0, shortcutsRect.height - headerHeight - 12);
    const int currentPage = std::max(0, selectedHomeIndex >= 0 ? selectedHomeIndex / HOME_SHORTCUT_PAGE_SIZE : 0);
    const int totalPages =
        (static_cast<int>(homeEntries.size()) + HOME_SHORTCUT_PAGE_SIZE - 1) / HOME_SHORTCUT_PAGE_SIZE;
    const int pageStart = currentPage * HOME_SHORTCUT_PAGE_SIZE;
    const int pageItemCount = std::min(HOME_SHORTCUT_PAGE_SIZE, static_cast<int>(homeEntries.size()) - pageStart);
    const int localSelectedIndex = (selectedHomeIndex >= pageStart && selectedHomeIndex < pageStart + pageItemCount)
                                       ? selectedHomeIndex - pageStart
                                       : -1;
    const std::string sectionLabel =
        std::string(tr(STR_SHORTCUTS_SECTION)) + " (" + std::to_string(homeEntries.size()) + ")";
    const std::string pageLabel = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);

    GUI.drawSubHeader(
        renderer,
        Rect{metrics.contentSidePadding, shortcutsRect.y, pageWidth - metrics.contentSidePadding * 2, headerHeight},
        sectionLabel.c_str(), pageLabel.c_str());
    GUI.drawButtonMenu(
        renderer, Rect{0, listTop, pageWidth, listHeight}, pageItemCount, localSelectedIndex,
        [&homeEntries, pageStart](const int index) { return getHomeShortcutTitle(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) { return getHomeShortcutIcon(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) { return getHomeShortcutSubtitle(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) {
          return showHomeShortcutAccessory(homeEntries[pageStart + index]);
        });
  }

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onAppsOpen() { activityManager.goToApps(); }

void HomeActivity::onReadingStatsOpen() {
  activityManager.replaceActivity(std::make_unique<ReadingStatsActivity>(renderer, mappedInput));
}

void HomeActivity::onSyncDayOpen() {
  activityManager.replaceActivity(std::make_unique<SyncDayActivity>(renderer, mappedInput));
}

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
