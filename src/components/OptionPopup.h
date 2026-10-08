#pragma once
#include <I18n.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/PopupViewport.h"
#include "components/UiAppHelpers.h"

// Modal picker/message with a clipped, scrollable content area. Only visible
// options enter the SDK's double-buffered touch interaction table.
class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) ownedStrings[i] = I18N.get(optionIds[i]);
    activate(currentIndex, std::move(onSelect));
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) ownedStrings[i] = options[i];
    activate(currentIndex, std::move(onSelect));
  }

  void show(const char* titleStr, const char* headlineStr, const char* const* options, int optionCount,
            int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    headline = headlineStr ? headlineStr : "";
  }

  void showMessage(const char* titleStr, const char* messageStr, const char* const* options, int optionCount,
                   int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    message = messageStr ? messageStr : "";
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings = options;
    activate(currentIndex, std::move(onSelect));
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate);
  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const;
  void render(const GfxRenderer& renderer) const;
  bool isActive() const { return active; }
  void dismiss() {
    active = false;
    onSelectCallback = nullptr;
  }

 private:
  // Capacity limits visible touch targets, never the number of choices.
  static constexpr size_t INTERACTION_CAPACITY = 32;
  static constexpr freeink::ui::ActionId ACTION_OPTION = 1;
  static constexpr freeink::ui::ActionId ACTION_CHROME = 2;
  void activate(int currentIndex, std::function<void(int)> onSelect);

  bool active = false;
  std::string title;
  std::string headline;
  std::string message;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
  // Sized on show(), then updated without allocating during rendering.
  mutable std::vector<PopupRowBounds> rows;
  mutable PopupViewport viewport;
  mutable bool firstRender = true;
  mutable freeink::ui::InteractionBuffer<INTERACTION_CAPACITY> interactions;
  mutable std::atomic<bool> uiReady{false};
};
