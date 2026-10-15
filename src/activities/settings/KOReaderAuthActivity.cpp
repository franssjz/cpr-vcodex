#include "KOReaderAuthActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/NetworkMemory.h"
#include "util/TimeUtils.h"

namespace {
void prepareMemoryBeforeAuthNetwork(GfxRenderer& renderer, const char* stage) {
  NetworkMemory::prepareBeforeNetwork(renderer, "KOSync", stage);
}

void restoreMemoryAfterAuthNetwork(GfxRenderer& renderer, const char* stage) {
  NetworkMemory::restoreAfterNetwork(renderer, "KOSync", stage);
}
}  // namespace
#include "network/WifiPowerSaveGuard.h"

void KOReaderAuthActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = AUTHENTICATING;
    statusMessage = tr(STR_SYNCING_TIME);
  }
  requestUpdateAndWait();

  const bool ntpSuccess = TimeUtils::syncTimeWithNtp(5000);
  LOG_DBG("KOSync", "Auth NTP sync %s", ntpSuccess ? "ok" : "timeout");
  TimeUtils::stopNtp();

  {
    RenderLock lock(*this);
    statusMessage = mode == Mode::SIGN_UP ? tr(STR_CREATING_ACCOUNT) : tr(STR_AUTHENTICATING);
  }
  requestUpdateAndWait();

  performAuthentication();
}

void KOReaderAuthActivity::performAuthentication() {
  prepareMemoryBeforeAuthNetwork(renderer, "before_authenticate");
  WifiPowerSaveGuard psGuard;
  const auto result =
      mode == Mode::SIGN_UP
          ? KOReaderSyncClient::registerUser(profile.username, KOReaderCredentialStore::hashPassword(profile.password),
                                             KOReaderCredentialStore::resolveBaseUrl(profile.serverUrl))
          : KOReaderSyncClient::authenticate();
  // Only a rejected preflight is safe to replay: a later transport failure may
  // already have created the account. The resumed activity cannot re-arm this.
  if (result == KOReaderSyncClient::NETWORK_ERROR &&
      KOReaderRecovery::shouldRecover(resumedAfterMemoryRecovery, KOReaderSyncClient::canRecoverFromLowMemory()) &&
      profileIndex >= 0 && static_cast<size_t>(profileIndex) < KOReaderRecovery::MAX_PROFILES) {
    const KOReaderRecoveryRequest request{
        mode == Mode::SIGN_UP ? KOReaderRecoveryOperation::Register : KOReaderRecoveryOperation::Authenticate,
        static_cast<uint8_t>(profileIndex), 0,
        KOReaderRecovery::profileIdentity(profile.username,
                                          KOReaderCredentialStore::resolveBaseUrl(profile.serverUrl))};
    silentRestartToKOReader(request);
  }
  restoreMemoryAfterAuthNetwork(renderer, "after_authenticate_restore");

  {
    RenderLock lock(*this);
    if (result == KOReaderSyncClient::OK) {
      if (mode == Mode::AUTHENTICATE && KOReaderSyncClient::usesKosyncSubdirectory() &&
          KOREADER_STORE.getMatchMethod() != DocumentMatchMethod::BINARY) {
        KOREADER_STORE.setMatchMethod(DocumentMatchMethod::BINARY);
        KOREADER_STORE.saveToFile();
        LOG_INF("KOSync", "Detected CWA /kosync server, switched document matching to Binary");
      }
      state = SUCCESS;
      statusMessage = mode == Mode::SIGN_UP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS);
    } else {
      state = FAILED;
      errorMessage =
          result == KOReaderSyncClient::USER_EXISTS ? tr(STR_USERNAME_TAKEN) : KOReaderSyncClient::errorString(result);
      const char* detail = KOReaderSyncClient::lastFailureDetail();
      if (detail && detail[0]) {
        errorMessage += " - ";
        errorMessage += detail;
      }
    }
  }
  requestUpdate();
}

void KOReaderAuthActivity::onEnter() {
  Activity::onEnter();

  // Authentication uses the active profile; registration may use a different
  // saved profile supplied by its editor. Pin that identity before networking.
  if (mode == Mode::AUTHENTICATE && profileIndex < 0) {
    profileIndex = KOREADER_STORE.getActiveIndex();
    if (profileIndex >= 0) {
      if (const auto* saved = KOREADER_STORE.getProfile(static_cast<size_t>(profileIndex))) profile = *saved;
    }
  }

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void KOReaderAuthActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void KOReaderAuthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 mode == Mode::SIGN_UP ? tr(STR_SIGN_UP) : tr(STR_KOREADER_AUTH));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  if (state == AUTHENTICATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.c_str());
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top,
                              mode == Mode::SIGN_UP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, tr(STR_SYNC_READY));
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, mode == Mode::SIGN_UP ? tr(STR_SIGNUP_FAILED) : tr(STR_AUTH_FAILED),
                              true, EpdFontFamily::BOLD);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, errorMessage.c_str(), pageWidth - 40, 4);
    for (size_t i = 0; i < lines.size(); ++i) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10 + static_cast<int>(i) * height, lines[i].c_str());
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void KOReaderAuthActivity::loop() {
  if (state == SUCCESS || state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
  }
}
