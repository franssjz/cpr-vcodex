#pragma once
#include <I18n.h>

#include <string>

#include "activities/Activity.h"

class QrDisplayActivity final : public Activity {
 public:
  explicit QrDisplayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& textPayload)
      : Activity("QrDisplay", renderer, mappedInput), textPayload(textPayload) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }

 private:
  std::string textPayload;
};
