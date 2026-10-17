#pragma once

// Apply opds_feed_loading_fixture.patch only in an isolated simulator copy.
// This opens the real OPDS activity; requests must stay on loopback.
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "activities/browser/OpdsBookBrowserActivity.h"
#include "util/ScreenshotUtil.h"

inline void startOpdsFeedLoadingFixture(GfxRenderer& renderer, MappedInputManager& input) {
  const std::string url = std::getenv("CPR_OPDS_FEED_URL");
  if (url.rfind("http://127.0.0.1:", 0) != 0) std::abort();
  WiFi.begin("Loopback fixture");  // Simulator only; no access point is contacted.
  OpdsServer server{"OPDS loading fixture", url, "", ""};
  activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, input, std::move(server)));
}

// Called only by the isolated fixture patch, while fetchFeed holds RenderLock.
// Scheduled SDL captures run on the blocked main loop; snapshot the already
// rendered framebuffer here to observe Loading during a synchronous fetch.
inline void captureOpdsFeedLoadingFixture(GfxRenderer& renderer) {
  if (!std::getenv("CPR_OPDS_FEED_URL")) return;
  static unsigned capture = 0;
  char filename[64];
  std::snprintf(filename, sizeof(filename), "/opds-loading-%u.bmp", ++capture);
  if (!ScreenshotUtil::saveFramebufferAsBmp(filename, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    std::abort();
  }
}
