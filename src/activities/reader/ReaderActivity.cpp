#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "KOReaderCredentialStore.h"
#include "SilentRestart.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "components/UITheme.h"

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; the dispatcher lives only until the
  // format reader replaces it, but a failed allocation must still be reported.
  auto activity = makeUniqueNoThrow<ReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

bool ReaderActivity::isBmpFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path);
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path, bool& uncached) {
  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("READER", "OOM: EPUB object");
    return nullptr;
  }
  uncached = !Storage.exists((epub->getCachePath() + "/book.bin").c_str());
  if (uncached) GUI.drawPopup(renderer, tr(STR_INDEXING));
  bool loaded = false;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = epub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (loaded) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load EPUB");
  loadProtectionError = epub->getProtectionError();
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
  if (!xtc) {
    LOG_ERR("READER", "OOM: XTC object");
    return nullptr;
  }
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  auto txt = makeUniqueNoThrow<Txt>(path, "/.crosspoint");
  if (!txt) {
    LOG_ERR("READER", "OOM: TXT object");
    return nullptr;
  }
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : FsHelpers::extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub, const bool allowFastRefresh) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;

  auto& sync = APP_STATE.koReaderSyncSession;
  const bool canAutoPull =
      SETTINGS.koSyncAutoPullOnOpen && KOREADER_STORE.hasCredentials() && !initialBookmark.enabled && !sync.active;
  if (canAutoPull) {
    sync.clear();
    sync.active = true;
    sync.epubPath = epubPath;
    sync.spineIndex = 0;
    sync.page = 0;
    sync.totalPagesInSpine = 0;
    sync.intent = KOReaderSyncIntentState::AUTO_PULL;
    sync.outcome = KOReaderSyncOutcomeState::PENDING;
    sync.autoPullEpubPath = epubPath;
    APP_STATE.saveToFile();

    LOG_DBG("READER", "Auto-pull KOReader sync before opening EPUB: %s", epubPath.c_str());
    activityManager.goToKOReaderSync();
    return;
  }

  auto reader = makeUniqueNoThrow<EpubReaderActivity>(
      renderer, mappedInput, std::move(epub), initialBookmark.enabled ? initialBookmark.spineIndex : -1,
      initialBookmark.enabled ? static_cast<int>(initialBookmark.page) : -1,
      initialBookmark.enabled && initialBookmark.hasVisibleTextOffset
          ? std::optional<uint32_t>(initialBookmark.visibleTextOffset)
          : std::nullopt,
      allowFastRefresh);
  if (!reader) {
    LOG_ERR("READER", "OOM: EPUB reader activity");
    goToLibrary(epubPath);
    return;
  }
  activityManager.replaceActivity(std::move(reader));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  auto viewer = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, path);
  if (!viewer) {
    LOG_ERR("READER", "OOM: bitmap viewer activity");
    goToLibrary(path);
    return;
  }
  activityManager.replaceActivity(std::move(viewer));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  auto reader = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(xtc), allowFastInitialRefresh);
  if (!reader) {
    LOG_ERR("READER", "OOM: XTC reader activity");
    goToLibrary(xtcPath);
    return;
  }
  activityManager.replaceActivity(std::move(reader));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  auto reader = makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(txt), allowFastInitialRefresh);
  if (!reader) {
    LOG_ERR("READER", "OOM: TXT reader activity");
    goToLibrary(txtPath);
    return;
  }
  activityManager.replaceActivity(std::move(reader));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (!Storage.exists(initialBookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", initialBookPath.c_str());
    goToLibrary(initialBookPath);
    return;
  }

  currentBookPath = initialBookPath;
  if (APP_STATE.koReaderSyncSession.active && APP_STATE.koReaderSyncSession.epubPath == initialBookPath) {
    LOG_DBG("READER", "Opening EPUB with pending KOReader sync outcome=%d",
            static_cast<int>(APP_STATE.koReaderSyncSession.outcome));
  }
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    bool uncached = false;
    auto epub = loadEpub(initialBookPath, uncached);
    if (!epub) {
      if (handleLoadFailure()) return;
      goToLibrary(initialBookPath);
      return;
    }
    // A freshly indexed book already paid for a popup + full render; the fast
    // first refresh is only for the boot -> last-book handoff on a cached book.
    onGoToEpubReader(std::move(epub), allowFastInitialRefresh && !uncached);
  }
}

bool ReaderActivity::handleLoadFailure() {
  if (loadProtectionError.empty()) return false;
  StrId message = StrId::STR_DRM_PROTECTED_FILE;
  const bool offerSync = loadProtectionError == "loan date unverified";
  if (loadProtectionError == "access expired") message = StrId::STR_LOAN_EXPIRED;
  if (offerSync) message = StrId::STR_LOAN_TIME_UNVERIFIED;
  const char* options[] = {offerSync ? tr(STR_CLOCK_SYNC_NOW) : tr(STR_OK_BUTTON), tr(STR_OK_BUTTON)};
  loadFailurePopup.showMessage("", I18N.get(message), options, offerSync ? 2 : 1, 0, [this, offerSync](int index) {
    if (offerSync && index == 0)
      beginLoanTimeSync();
    else
      goToLibrary(initialBookPath);
  });
  requestUpdate();
  return true;
}

void ReaderActivity::beginLoanTimeSync() {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    goToLibrary(initialBookPath);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
      goToLibrary(initialBookPath);
      return;
    }
    GUI.drawPopup(renderer, tr(STR_SYNCING_TIME));
    const bool synced = trustedtime::syncNow(5000);
    WiFi.disconnect(false);
    delay(30);
    if (!synced) {
      const char* options[] = {tr(STR_RETRY), tr(STR_OK_BUTTON)};
      loadFailurePopup.showMessage("", tr(STR_CLOCK_SYNC_FAIL), options, 2, 0, [this](int index) {
        if (index == 0)
          beginLoanTimeSync();
        else
          goToLibrary(initialBookPath);
      });
      requestUpdate();
      return;
    }
    APP_STATE.openEpubPath = initialBookPath;
    APP_STATE.saveToFile();
    silentRestartToReader();
    activityManager.goToReader(initialBookPath);
  });
}

void ReaderActivity::loop() {
  if (loadFailurePopup.isActive()) {
    loadFailurePopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }
  goToLibrary(initialBookPath);
}

void ReaderActivity::render(RenderLock&&) {
  if (!loadFailurePopup.isActive()) return;
  renderer.clearScreen();
  loadFailurePopup.processRender(renderer, mappedInput);
}
