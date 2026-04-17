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
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadMeActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"

namespace {
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
constexpr int HOME_SHORTCUT_PAGE_SIZE = 4;

struct HomeShortcutEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
  bool isOpds = false;
};

std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}

std::string getBookTitleFromPath(const std::string& path) {
  const size_t slashPos = path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? path : path.substr(slashPos + 1);
  const size_t dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}

RecentBook toRecentBook(const FavoriteBook& book) {
  RecentBook recentBook{book.bookId, book.path, book.title, book.author, book.coverBmpPath};
  if (recentBook.title.empty()) {
    recentBook.title = getBookTitleFromPath(recentBook.path);
  }
  return recentBook;
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
  return entry.definition ? ShortcutUiMetadata::getSubtitle(*entry.definition) : "";
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
  return entry.definition && ShortcutUiMetadata::showAccessory(*entry.definition);
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  return static_cast<int>(recentBooks.size() + getHomeShortcutEntries(hasOpdsUrl).size());
}

void HomeActivity::loadHomeCarouselBooks(const int maxBooks) {
  recentBooks.clear();
  if (SETTINGS.homeCarouselSource == CrossPointSettings::HOME_CAROUSEL_FAVORITES) {
    const auto& books = FAVORITES.getBooks();
    recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));
    for (const FavoriteBook& book : books) {
      if (static_cast<int>(recentBooks.size()) >= maxBooks) {
        break;
      }
      if (!Storage.exists(book.path.c_str())) {
        continue;
      }
      recentBooks.push_back(toRecentBook(book));
    }
    return;
  }

  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (static_cast<int>(recentBooks.size()) >= maxBooks) {
      break;
    }
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
  }
}

bool HomeActivity::needsRecentCoverLoad(const int coverHeight) const {
  for (const RecentBook& book : recentBooks) {
    if (book.coverBmpPath.empty()) {
      continue;
    }

    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (!Storage.exists(coverPath.c_str()) &&
        (FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path))) {
      return true;
    }
  }
  return false;
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
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          epub.load(false, true);

          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          const bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          needsRefresh = true;
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            const bool success = xtc.generateThumbBmp(coverHeight);
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

  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;
  firstRenderDone = false;
  recentsLoading = false;
  recentsLoaded = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadHomeCarouselBooks(metrics.homeRecentBooksCount);
  recentsLoaded = !needsRecentCoverLoad(metrics.homeCoverHeight);

  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
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

  const size_t bufferSize = renderer.getBufferSize();
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
  const int recentCount = static_cast<int>(recentBooks.size());
  const int homeCount = static_cast<int>(homeEntries.size());

  buttonNavigator.onNextPress([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousPress([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, menuCount, recentCount, homeCount] {
    if (menuCount <= 0) {
      return;
    }

    if (homeCount <= HOME_SHORTCUT_PAGE_SIZE) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    } else if (selectorIndex < recentCount) {
      selectorIndex = recentCount;
    } else {
      const int selectedHomeIndex = selectorIndex - recentCount;
      selectorIndex = recentCount + ButtonNavigator::nextPageIndex(selectedHomeIndex, homeCount, HOME_SHORTCUT_PAGE_SIZE);
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, menuCount, recentCount, homeCount] {
    if (menuCount <= 0) {
      return;
    }

    if (homeCount <= HOME_SHORTCUT_PAGE_SIZE) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    } else if (selectorIndex < recentCount) {
      selectorIndex = recentCount + ButtonNavigator::previousPageIndex(0, homeCount, HOME_SHORTCUT_PAGE_SIZE);
    } else {
      const int selectedHomeIndex = selectorIndex - recentCount;
      selectorIndex =
          recentCount + ButtonNavigator::previousPageIndex(selectedHomeIndex, homeCount, HOME_SHORTCUT_PAGE_SIZE);
    }
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < recentBooks.size()) {
      if (SETTINGS.homeCarouselSource == CrossPointSettings::HOME_CAROUSEL_RECENTS &&
          mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
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
                loadHomeCarouselBooks(metrics.homeRecentBooksCount);
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
        case ShortcutId::ReadingProfile:
          startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput),
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
        case ShortcutId::Favorites:
          startActivityForResult(std::make_unique<FavoritesAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestUpdate(); });
          break;
        case ShortcutId::Flashcards:
          startActivityForResult(std::make_unique<FlashcardsAppActivity>(renderer, mappedInput),
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

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  const auto homeEntries = getHomeShortcutEntries(hasOpdsUrl);
  const int selectedHomeIndex = selectorIndex - static_cast<int>(recentBooks.size());
  const Rect shortcutsRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
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
    const int localSelectedIndex =
        (selectedHomeIndex >= pageStart && selectedHomeIndex < pageStart + pageItemCount) ? selectedHomeIndex - pageStart
                                                                                            : -1;
    const std::string sectionLabel =
        std::string(tr(STR_SHORTCUTS_SECTION)) + " (" + std::to_string(homeEntries.size()) + ")";
    const std::string pageLabel = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);

    GUI.drawSubHeader(renderer,
                      Rect{metrics.contentSidePadding, shortcutsRect.y, pageWidth - metrics.contentSidePadding * 2,
                           headerHeight},
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
    if (!recentsLoaded) {
      requestUpdate();
    }
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
