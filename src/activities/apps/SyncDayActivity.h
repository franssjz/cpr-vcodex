#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class SyncDayActivity final : public Activity {
  bool wifiConnectedOnEnter = false;
  bool connectedInActivity = false;
  bool syncing = false;
  bool lastSyncSucceeded = false;
  bool lastSyncFailed = false;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void openWifiSelection(bool allowAutoConnect);
  void openManualDateSelection();
  void openTimeZoneSelection();
  void syncTime();
  bool isWifiConnected() const;
  std::string getStatusMessage() const;

 public:
  explicit SyncDayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SyncDay", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return syncing; }
};
