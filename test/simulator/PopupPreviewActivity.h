#pragma once

// Simulator-only stress fixture. Included by popup_preview.patch in an isolated
// simulator checkout, never by production firmware.
#include <cstdlib>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class PopupPreviewActivity : public Activity {
  OptionPopup popup;

 public:
  PopupPreviewActivity(GfxRenderer& renderer, MappedInputManager& input) : Activity("PopupPreview", renderer, input) {}

  void onEnter() override {
    Activity::onEnter();
    const char* orientation = std::getenv("CPR_POPUP_ORIENTATION");
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation ? std::atoi(orientation) : 0));
    const char* scenario = std::getenv("CPR_POPUP_PREVIEW");
    if (scenario && std::string(scenario) == "message") {
      std::string body;
      for (int i = 1; i <= 35; ++i) body += "Line " + std::to_string(i) + ": complete message text.\n";
      const char* options[] = {"Cancel", "Confirm"};
      popup.showMessage("Long message", body.c_str(), options, 2, 0, [this](int index) {
        printf("POPUP_SELECTED=%d\n", index);
        requestUpdate();
      });
    } else {
      std::vector<std::string> options;
      for (int i = 0; i < 40; ++i)
        options.push_back("Option " + std::to_string(i + 1) + " with a label that wraps across multiple lines");
      popup.show(StrId::STR_SLEEP_SCREEN, options, 39, [this](int index) {
        printf("POPUP_SELECTED=%d\n", index);
        requestUpdate();
      });
    }
    requestUpdate();
  }

  void loop() override {
    popup.handleInput(mappedInput, [this] { requestUpdate(); });
  }
  void render(RenderLock&&) override {
    renderer.clearScreen();
    if (!popup.processRender(renderer, mappedInput)) renderer.displayBuffer();
  }
};
