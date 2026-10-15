#pragma once

// Test entry points only: apply koreader_network_fixture.patch in an isolated
// X4 simulator checkout. Network profiles must point to a loopback endpoint.
#include <WiFi.h>

#include <cstdlib>
#include <string>

#include "KOReaderCredentialStore.h"
#include "activities/settings/KOReaderAuthActivity.h"
#include "activities/settings/KOReaderProfileEditActivity.h"
#include "activities/settings/KOReaderSettingsActivity.h"

inline void startKOReaderNetworkFixture(GfxRenderer& renderer, MappedInputManager& input) {
  const std::string screen = std::getenv("CPR_KOREADER_SCREEN");
  const char* indexText = std::getenv("CPR_KOREADER_PROFILE_INDEX");
  const int index = indexText ? std::atoi(indexText) : 0;
  if (screen == "settings") {
    activityManager.replaceActivity(std::make_unique<KOReaderSettingsActivity>(renderer, input));
    return;
  }
  if (screen == "edit") {
    activityManager.replaceActivity(std::make_unique<KOReaderProfileEditActivity>(renderer, input, index));
    return;
  }
  const bool signUp = screen == "signup" || screen == "signup-resumed";
  const bool resumed = screen == "auth-resumed" || screen == "signup-resumed";
  if (!signUp && screen != "auth" && screen != "auth-resumed") std::abort();
  const auto* profile = KOREADER_STORE.getProfile(signUp ? index : KOREADER_STORE.getActiveIndex());
  if (!profile) std::abort();
  const auto url = KOReaderCredentialStore::resolveBaseUrl(profile->serverUrl);
  // Fail closed even if this fixture is launched outside the Python harness.
  if (url.rfind("http://127.0.0.1:", 0) != 0 && url.rfind("https://127.0.0.1:", 0) != 0) std::abort();
  WiFi.begin("Loopback fixture");  // Simulator WiFi only; does not contact an access point.
  activityManager.replaceActivity(std::make_unique<KOReaderAuthActivity>(
      renderer, input, signUp ? KOReaderAuthActivity::Mode::SIGN_UP : KOReaderAuthActivity::Mode::AUTHENTICATE,
      *profile, signUp ? index : KOREADER_STORE.getActiveIndex(), resumed));
}
