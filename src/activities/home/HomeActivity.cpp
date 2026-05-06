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
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"

namespace {
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
constexpr int HOME_SHORTCUT_PAGE_SIZE = 4;
constexpr int CAROUSEL_SHORTCUT_COUNT = 5;

struct HomeShortcutEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
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

std::vector<HomeShortcutEntry> getHomeShortcutEntries(const bool hasOpdsServers) {
  std::vector<HomeShortcutEntry> entries;
  entries.push_back(HomeShortcutEntry{nullptr, true});

  for (const auto& definition : getShortcutDefinitions()) {
    if (definition.id == ShortcutId::OpdsBrowser && !hasOpdsServers) {
      continue;
    }
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

  return entries;
}

// Builds the carousel shortcut list (up to CAROUSEL_SHORTCUT_COUNT slots):
//   slots 0..(n-3) : first 3 home shortcuts that are not Settings or Apps
//   slot  (n-2)    : Settings — always pinned, sourced from definitions if not
//                    configured as a home shortcut, so the user is never locked out
//   slot  (n-1)    : Apps hub — always pinned last
std::vector<HomeShortcutEntry> buildCarouselEntries(const std::vector<HomeShortcutEntry>& all) {
  constexpr int kPinnedCount = 2;
  constexpr int kVariableSlots = CAROUSEL_SHORTCUT_COUNT - kPinnedCount;

  std::vector<HomeShortcutEntry> result;
  HomeShortcutEntry appsEntry{nullptr, true};
  HomeShortcutEntry settingsEntry;
  bool foundSettings = false;

  for (const auto& e : all) {
    if (e.isAppsHub) {
      appsEntry = e;
    } else if (e.definition && e.definition->id == ShortcutId::Settings) {
      settingsEntry = e;
      foundSettings = true;
    } else if (static_cast<int>(result.size()) < kVariableSlots) {
      result.push_back(e);
    }
  }

  if (!foundSettings) {
    for (const auto& def : getShortcutDefinitions()) {
      if (def.id == ShortcutId::Settings) {
        settingsEntry = HomeShortcutEntry{&def};
        foundSettings = true;
        break;
      }
    }
  }

  if (foundSettings) {
    result.push_back(settingsEntry);
  }
  result.push_back(appsEntry);
  return result;
}

std::string getHomeShortcutTitle(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return tr(STR_APPS);
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
  return entry.definition ? entry.definition->icon : UIIcon::Folder;
}

bool showHomeShortcutAccessory(const HomeShortcutEntry& entry) {
  return entry.definition && ShortcutUiMetadata::showAccessory(*entry.definition);
}

bool isLyraCarouselTheme() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) ==
         CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
}

int wrapBookIndex(int index, int bookCount) {
  if (bookCount <= 0) {
    return 0;
  }
  while (index < 0) {
    index += bookCount;
  }
  return index % bookCount;
}

bool hasLegacyHomeThumb(const RecentBook& book) {
  if (book.coverBmpPath.empty()) {
    return true;
  }
  const std::string coverPath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselMetrics::values.homeCoverHeight);
  return Storage.exists(coverPath.c_str());
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  auto entries = getHomeShortcutEntries(hasOpdsServers);
  if (isLyraCarouselTheme()) {
    entries = buildCarouselEntries(entries);
  }
  return static_cast<int>(recentBooks.size()) + static_cast<int>(entries.size());
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

    const bool missingThumb = isLyraCarouselTheme()
                                  ? !hasLegacyHomeThumb(book)
                                  : !Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight).c_str());
    if (missingThumb && (FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path))) {
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
    if (isLyraCarouselTheme() && progress != lastCarouselBookIndex) {
      progress++;
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      const bool missingThumb =
          isLyraCarouselTheme() ? !hasLegacyHomeThumb(book)
                                : !Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight).c_str());
      if (missingThumb) {
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          epub.load(isLyraCarouselTheme(), true);

          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect,
                                10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
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
            GUI.fillPopupProgress(renderer, popupRect,
                                  10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
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
    if (isLyraCarouselTheme()) {
      carouselFramesReady = false;
      freeCarouselFrames();
      preRenderCarouselFrames();
    }
    requestUpdate();
  }
}

void HomeActivity::scheduleCarouselCoverLoadIfNeeded() {
  if (!isLyraCarouselTheme() || recentBooks.empty() || lastCarouselBookIndex < 0 ||
      lastCarouselBookIndex >= static_cast<int>(recentBooks.size())) {
    return;
  }
  const RecentBook& book = recentBooks[lastCarouselBookIndex];
  if (!book.coverBmpPath.empty() && !hasLegacyHomeThumb(book) &&
      (FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path))) {
    recentsLoaded = false;
    requestUpdate();
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  selectorIndex = 0;
  firstRenderDone = false;
  recentsLoading = false;
  recentsLoaded = false;
  lastCarouselBookIndex = 0;
  carouselFramesReady = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadHomeCarouselBooks(metrics.homeRecentBooksCount);
  recentsLoaded = !needsRecentCoverLoad(metrics.homeCoverHeight);

  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
  freeCoverBuffer();
  freeCarouselFrames();
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

void HomeActivity::freeCarouselFrames() {
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (carouselFrames[i]) {
      free(carouselFrames[i]);
      carouselFrames[i] = nullptr;
    }
    carouselFrameBookIdx[i] = -1;
  }
  carouselFramesReady = false;
}

void HomeActivity::renderCarouselFrame(int slot, int bookIndex) {
  if (slot < 0 || slot >= kCarouselFrameCount || recentBooks.empty()) {
    return;
  }

  const size_t bufferSize = renderer.getBufferSize();
  if (!carouselFrames[slot]) {
    carouselFrames[slot] = static_cast<uint8_t*>(malloc(bufferSize));
    if (!carouselFrames[slot]) {
      return;
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  bool localCoverRendered = false;
  bool localCoverBufferStored = false;
  bool localBufferRestored = false;
  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  // setPreRenderIndex sets lastCarouselSelectorIndex so drawRecentBookCover
  // picks the correct center book. We pass bookCount (not safeBookIndex) as
  // selectorIndex so inCarouselRow=false and the frame is stored with a thin
  // outline; drawCarouselBorder() overlays the thick selection border at
  // display time only when the carousel row is actually active.
  LyraCarouselTheme::setPreRenderIndex(safeBookIndex);
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, bookCount, localCoverRendered, localCoverBufferStored, localBufferRestored,
                          [] { return false; });

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return;
  }
  memcpy(carouselFrames[slot], frameBuffer, bufferSize);
  carouselFrameBookIdx[slot] = safeBookIndex;
}

void HomeActivity::preRenderCarouselFrames() {
  if (!isLyraCarouselTheme() || recentBooks.empty()) {
    return;
  }

  freeCoverBuffer();
  const int bookCount = static_cast<int>(recentBooks.size());
  const int centerIdx = wrapBookIndex(lastCarouselBookIndex, bookCount);
  // Render only the center frame into slot 0. Adjacent frames (prev/next) are
  // filled lazily by updateSlidingWindowCache() after the first paint completes,
  // so the user sees their selected book immediately without 3 SD card reads.
  renderCarouselFrame(0, centerIdx);
  carouselFramesReady = (carouselFrames[0] != nullptr);
}

void HomeActivity::updateSlidingWindowCache(int centerIdx, int bookCount) {
  if (!isLyraCarouselTheme() || bookCount <= 0) {
    return;
  }

  for (int offset = -1; offset <= 1; ++offset) {
    const int bookIdx = wrapBookIndex(centerIdx + offset, bookCount);
    bool hasFrame = false;
    for (int slot = 0; slot < kCarouselFrameCount; ++slot) {
      if (carouselFrames[slot] && carouselFrameBookIdx[slot] == bookIdx) {
        hasFrame = true;
        break;
      }
    }
    if (hasFrame) {
      continue;
    }

    int slotToUse = -1;
    for (int slot = 0; slot < kCarouselFrameCount; ++slot) {
      if (!carouselFrames[slot]) {
        slotToUse = slot;
        break;
      }
    }
    if (slotToUse < 0) {
      slotToUse = offset < 0 ? 2 : 0;
    }
    renderCarouselFrame(slotToUse, bookIdx);
  }

  carouselFramesReady = carouselFrames[0] && carouselFrames[1] && carouselFrames[2];
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  auto homeEntries = getHomeShortcutEntries(hasOpdsServers);
  if (isLyraCarouselTheme()) {
    homeEntries = buildCarouselEntries(homeEntries);
  }
  const int recentCount = static_cast<int>(recentBooks.size());
  const int homeCount = static_cast<int>(homeEntries.size());

  if (isLyraCarouselTheme()) {
    // Carousel navigation: Left/Right move within the focused row;
    // Up/Down toggle between the carousel row and the shortcuts row.
    const bool inCarouselRow = recentCount > 0 && selectorIndex < recentCount;

    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (inCarouselRow) {
        selectorIndex = (selectorIndex + recentCount - 1) % recentCount;
        requestUpdate();
      } else if (homeCount > 0) {
        const int homeIdx = selectorIndex - recentCount;
        selectorIndex = recentCount + (homeIdx + homeCount - 1) % homeCount;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (inCarouselRow) {
        selectorIndex = (selectorIndex + 1) % recentCount;
        requestUpdate();
      } else if (homeCount > 0) {
        const int homeIdx = selectorIndex - recentCount;
        selectorIndex = recentCount + (homeIdx + 1) % homeCount;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (inCarouselRow && homeCount > 0) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = recentCount;  // land on first shortcut
        requestUpdate();
      } else if (!inCarouselRow && recentCount > 0) {
        selectorIndex = wrapBookIndex(lastCarouselBookIndex, recentCount);
        requestUpdate();
      }
    }
  } else {
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
  }

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
                freeCarouselFrames();
                if (isLyraCarouselTheme()) {
                  lastCarouselBookIndex = selectorIndex < static_cast<int>(recentBooks.size()) ? selectorIndex : 0;
                  preRenderCarouselFrames();
                }
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
        case ShortcutId::OpdsBrowser:
          onOpdsBrowserOpen();
          break;
      }
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int recentCount = static_cast<int>(recentBooks.size());
  const bool carouselTheme = isLyraCarouselTheme();
  const bool wasFirstRenderDone = firstRenderDone;
  const bool inCarouselRow = carouselTheme && selectorIndex < recentCount;
  if (inCarouselRow) {
    lastCarouselBookIndex = selectorIndex;
    scheduleCarouselCoverLoadIfNeeded();
  }

  bool usedCarouselFrame = false;
  if (carouselTheme && carouselFramesReady && !recentBooks.empty()) {
    const int centerIdx = wrapBookIndex(lastCarouselBookIndex, recentCount);
    int frameSlot = -1;
    for (int slot = 0; slot < kCarouselFrameCount; ++slot) {
      if (carouselFrames[slot] && carouselFrameBookIdx[slot] == centerIdx) {
        frameSlot = slot;
        break;
      }
    }
    if (frameSlot < 0) {
      frameSlot = 1;
      renderCarouselFrame(frameSlot, centerIdx);
    }

    uint8_t* frameBuffer = renderer.getFrameBuffer();
    if (frameBuffer && carouselFrames[frameSlot]) {
      memcpy(frameBuffer, carouselFrames[frameSlot], renderer.getBufferSize());
      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             inCarouselRow);
      usedCarouselFrame = true;
    }
  }

  if (!usedCarouselFrame) {
    renderer.clearScreen();
    bool bufferRestored = coverBufferStored && restoreCoverBuffer();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
    HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  auto homeEntries = getHomeShortcutEntries(hasOpdsServers);
  if (carouselTheme) {
    homeEntries = buildCarouselEntries(homeEntries);
  }
  const int selectedHomeIndex = selectorIndex - static_cast<int>(recentBooks.size());
  const Rect shortcutsRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
                           pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing +
                                         metrics.buttonHintsHeight + metrics.verticalSpacing)};

  const int shortcutDisplayCount = static_cast<int>(homeEntries.size());

  if (carouselTheme || shortcutDisplayCount <= HOME_SHORTCUT_PAGE_SIZE) {
    GUI.drawButtonMenu(
        renderer, shortcutsRect, shortcutDisplayCount, selectedHomeIndex,
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

  const auto labels = carouselTheme
                          ? mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                          : mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (usedCarouselFrame) {
    updateSlidingWindowCache(lastCarouselBookIndex, recentCount);
  }

  if (wasFirstRenderDone && carouselTheme && recentsLoaded && !carouselFramesReady && !recentBooks.empty()) {
    preRenderCarouselFrames();
    if (carouselFramesReady) {
      requestUpdate();
    }
  }

  if (!firstRenderDone) {
    firstRenderDone = true;
    if (!recentsLoaded || (carouselTheme && recentsLoaded && !carouselFramesReady)) {
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
