#include "OpdsBookBrowserActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "components/CatalogScreens.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/PluginHttp.h"
#include "util/StringUtils.h"
#include "util/TaskWatchdog.h"
#include "util/UrlUtils.h"

namespace fui = freeink::ui;

OpdsBookBrowserActivity::OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 OpdsServer server)
    : CatalogActivity("OpdsBookBrowser", renderer, mappedInput), server(std::move(server)) {}

void OpdsBookBrowserActivity::onEnter() {
  CatalogActivity::onEnter();
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
  releaseEntries();
  navigationHistory.clear();
  CatalogActivity::onExit();
}

void OpdsBookBrowserActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  const auto& entry = entries[index];
  entry.type == OpdsEntryType::BOOK ? downloadBook(entry) : navigateToEntry(entry);
}

void OpdsBookBrowserActivity::buildScreen(UiScreen& screen) {
  screenHeader(screen, server.name.empty() ? tr(STR_OPDS_BROWSER) : server.name.c_str());
  if (!buildStatusScreen(screen, /*boldError=*/false, /*showDownloadTotal=*/true)) buildBrowsingScreen(screen);
}

void OpdsBookBrowserActivity::buildBrowsingScreen(UiScreen& screen) {
  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the nav chevron and the row edge
  syncListViewport(screen, props);
  screen.list(props);
}

void OpdsBookBrowserActivity::drawFooter() {
  MappedInputManager::Labels labels;
  switch (state) {
    case State::BROWSING: {
      // Feeds open; books and the pager rows fetch, matching the plugin catalog.
      const char* confirmLabel = tr(STR_OPEN);
      if (!entries.empty()) {
        const bool onPager = (prevRowPresent && nav.selected == 0) ||
                             (nextRowPresent && nav.selected == static_cast<int>(entries.size()) - 1);
        if (onPager || entries[nav.selected].type == OpdsEntryType::BOOK) confirmLabel = tr(STR_FETCH);
      }
      const char* searchLabel = (!searchTemplate.empty() && nav.selected == 0) ? tr(STR_SEARCH) : tr(STR_DIR_UP);
      labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, searchLabel, tr(STR_DIR_DOWN));
      break;
    }
    case State::DOWNLOADING:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    case State::ERROR:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  // Loading has finished rendering. Keep font/cache and SD access away from
  // the renderer until the new catalog (or error) is ready.
  RenderLock lock(*this);
  if (server.url.empty()) {
    fail(StrId::STR_NO_SERVER_URL);
    return;
  }

  std::string url = UrlUtils::buildUrl(server.url, path);
  LOG_DBG("OPDS", "Fetching: %s", url.c_str());

  // Adapted from CrossInk 6a45458f: separate the TLS and XML allocation peaks.
  // This also handles entry through an already-connected Wi-Fi session, which
  // skips WifiSelectionActivity's font-memory trim.
  sdFontSystem.releaseForNetwork(renderer);
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();

  static constexpr const char* FEED_TMP_PATH = "/.crosspoint/opds-feed.tmp";
  HalFile feedFile;
  ScopedCleanup cleanup{[&feedFile] {
    if (feedFile) feedFile.close();
    if (Storage.exists(FEED_TMP_PATH) && !Storage.remove(FEED_TMP_PATH)) {
      LOG_ERR("OPDS", "Failed to remove temporary feed");
    }
  }};
  // Never parse an older response if a download or SD operation fails.
  if ((Storage.exists(FEED_TMP_PATH) && !Storage.remove(FEED_TMP_PATH)) ||
      !Storage.ensureDirectoryExists("/.crosspoint")) {
    LOG_ERR("OPDS", "Cannot prepare temporary feed");
    fail(StrId::STR_FETCH_FEED_FAILED);
    return;
  }
  if (url.rfind("https://", 0) == 0 && (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
                                        ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC)) {
    LOG_ERR("OPDS", "Low heap for feed (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  const auto download =
      HttpDownloader::downloadToFile(url, FEED_TMP_PATH, nullptr, nullptr, server.username, server.password);
  if (download != HttpDownloader::OK) {
    LOG_ERR("OPDS", "Feed download failed: %d", static_cast<int>(download));
    fail(StrId::STR_FETCH_FEED_FAILED);
    return;
  }

  // downloadToFile has closed both its file and HTTP/TLS before Expat exists.
  if (!Storage.openFileForRead("OPDS", FEED_TMP_PATH, feedFile)) {
    LOG_ERR("OPDS", "Cannot open temporary feed");
    fail(StrId::STR_FETCH_FEED_FAILED);
    return;
  }
  constexpr size_t READ_CHUNK = 1024;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(READ_CHUNK);
  if (!buffer) {
    LOG_ERR("OPDS", "OOM: feed read buffer");
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  OpdsParser parser;
  uint64_t remaining = feedFile.fileSize64();
  uint8_t chunksUntilYield = 0;
  while (remaining > 0 && parser) {
    const size_t requested = remaining < READ_CHUNK ? static_cast<size_t>(remaining) : READ_CHUNK;
    const int count = feedFile.read(buffer.get(), requested);
    if (count <= 0 || static_cast<size_t>(count) > requested) {
      LOG_ERR("OPDS", "Temporary feed read failed or ended early");
      fail(StrId::STR_FETCH_FEED_FAILED);
      return;
    }
    parser.write(buffer.get(), static_cast<size_t>(count));
    remaining -= static_cast<size_t>(count);
    if (++chunksUntilYield == 8) {
      // SD parsing no longer has socket waits that let the idle task run.
      resetTaskWatchdogIfSubscribed();
      delay(1);
      chunksUntilYield = 0;
    }
  }
  buffer.reset();
  feedFile.close();
  parser.flush();

  if (!parser) {
    fail(parser.memoryError() ? StrId::STR_MEMORY_ERROR : StrId::STR_PARSE_FEED_FAILED);
    return;
  }

  searchTemplate = parser.getSearchTemplate();
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  const bool feedTruncated = parser.truncated();
  // Reset selection before swapping in a potentially shorter feed.
  nav.reset();
  entries = std::move(parser).getEntries();

  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  prevRowPresent = !prevUrl.empty();
  nextRowPresent = !nextUrl.empty();
  if (prevRowPresent) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (nextRowPresent) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""});
  }
  if (feedTruncated) {
    LOG_INF("OPDS", "Feed truncated to fit memory");
  }

  state = entries.empty() ? State::ERROR : State::BROWSING;
  if (entries.empty()) errorMessage = tr(STR_NO_ENTRIES);
  rebuildRowItems();
  requestUpdate();
}

// Derives rowItems from entries. Called whenever entries changes
// (fetchFeed()/releaseEntries()) so buildBrowsingScreen() reuses the cached
// rows on every repaint instead of rebuilding them per render.
void OpdsBookBrowserActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(entries.size());
  for (const auto& entry : entries) {
    fui::ListItem item;
    item.label = entry.title.c_str();
    if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) item.subtitle = entry.author.c_str();
    if (entry.type == OpdsEntryType::NAVIGATION) item.value = ">";
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void OpdsBookBrowserActivity::releaseEntries() {
  // The app's interaction table holds row indices (and hit rects) for the old
  // entries; stop routing touches against it until the next render.
  closeRouting();
  std::vector<OpdsEntry>().swap(entries);
  std::vector<fui::ListItem>().swap(rowItems);
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  startBrowse();
}

void OpdsBookBrowserActivity::onBackButton() {
  if (state == State::CHECK_WIFI || navigationHistory.empty()) {
    activityManager.goBack();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    startBrowse();
  }
}

void OpdsBookBrowserActivity::startBrowse() {
  {
    RenderLock lock(*this);
    releaseEntries();
    searchTemplate.clear();
    prevRowPresent = nextRowPresent = false;
    nav.reset();
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
  }
  // Do not wait while holding RenderLock: the rendering task needs that mutex.
  requestUpdateAndWait();
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  beginDownload(book.title);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);
  // Downloads land in a per-server directory: "<base>/<server name>", where
  // <base> is opdsDownloadFolder when set and "/opds" otherwise. On failure to
  // prepare the directory, fall back to SD root so the download is never lost.
  const char* folder = SETTINGS.opdsDownloadFolder;  // "" => "/opds"
  std::string opdsDir = folder[0] != '\0' ? std::string(folder) : std::string("/opds");
  opdsDir += '/';
  opdsDir += server.name.empty() ? std::string("default") : StringUtils::sanitizeFilename(server.name);
  if (!Storage.ensureDirectoryExists(opdsDir.c_str())) {
    LOG_ERR("OPDS", "Failed to prepare directory %s, using SD root", opdsDir.c_str());
    opdsDir.clear();
  }

  // downloadToFile() needs a std::string, and titles are unbounded (a fixed
  // char[] would truncate). Cold path (a multi-second download follows), so one
  // reserve'd, in-place-appended owning string is the right call.
  std::string filename;
  filename.reserve(opdsDir.size() + 96);
  filename += opdsDir;
  filename += '/';
  filename += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  LOG_DBG("OPDS", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  // The selected book data is now copied into the download URL, filename, and
  // status line. Reclaim the catalog while TLS owns its record buffers; reload
  // the current feed when the transfer finishes.
  releaseEntries();

  // downloadFile() (CatalogActivity) releases font caches and checks the TLS heap floor.
  const auto result = downloadFile(downloadUrl, filename, server.username, server.password);
  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
    library::markLibraryIndexDirty();
  }
  finishDownload(result);
}

void OpdsBookBrowserActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = State::BROWSING;
    requestUpdate();
    return;
  }

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), pluginhttp::urlEncodeQuery(query));

  navigationHistory.push_back(currentPath);
  currentPath = url;

  startBrowse();
}
