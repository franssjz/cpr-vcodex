#include "DictionaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "DictionaryStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int DICTIONARY_ACTION_COUNT = 2;
constexpr int ACTION_DEFINITION_TEXT_SIZE = 0;
constexpr int ACTION_CLEAR_HISTORY = 1;
}  // namespace

void DictionaryActivity::onEnter() {
  Activity::onEnter();
  DICTIONARIES.ensureScanned();
  const int activeIndex = DICTIONARIES.getActiveIndex();
  selectedIndex = activeIndex >= 0 ? activeIndex + DICTIONARY_ACTION_COUNT : 0;
  requestUpdate();
}

void DictionaryActivity::selectCurrent() {
  if (selectedIndex == ACTION_DEFINITION_TEXT_SIZE) {
    const uint8_t nextSize =
        static_cast<uint8_t>((DICTIONARIES.getDefinitionTextSize() + 1) % DictionaryStore::DEF_TEXT_SIZE_COUNT);
    DICTIONARIES.setDefinitionTextSize(nextSize);
    requestUpdate();
    return;
  }
  if (selectedIndex == ACTION_CLEAR_HISTORY) {
    DICTIONARIES.clearHistory();
    GUI.drawPopup(renderer, tr(STR_CLEAR_HISTORY));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(650);
    requestUpdate();
    return;
  }

  const auto& entries = DICTIONARIES.getEntries();
  const int dictionaryIndex = selectedIndex - DICTIONARY_ACTION_COUNT;
  if (dictionaryIndex < 0 || dictionaryIndex >= static_cast<int>(entries.size())) return;
  const auto& entry = entries[dictionaryIndex];
  if (entry.compressed) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_COMPRESSED_UNSUPPORTED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1100);
    requestUpdate();
    return;
  }
  if (entry.missingFiles) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_MISSING_FILES));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1100);
    requestUpdate();
    return;
  }

  DICTIONARIES.setActiveIndex(dictionaryIndex);
  Rect popup;
  {
    RenderLock lock(*this);
    popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_PREPARING));
  }
  const bool ready = DICTIONARIES.prepareActive([this, &popup](int percent) {
    RenderLock lock(*this);
    GUI.fillPopupProgress(renderer, popup, percent);
  });
  GUI.drawPopup(renderer, ready ? tr(STR_DICTIONARY_READY) : tr(STR_DICTIONARY_PREPARE_FAILED));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  delay(900);
  requestUpdate();
}

void DictionaryActivity::loop() {
  const auto& entries = DICTIONARIES.getEntries();
  const int totalItems = static_cast<int>(entries.size()) + DICTIONARY_ACTION_COUNT;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectCurrent();
    return;
  }

  buttonNavigator.onNext([this, totalItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this, totalItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
      requestUpdate();
    }
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
      requestUpdate();
    }
  });
}

void DictionaryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& entries = DICTIONARIES.getEntries();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const std::string activeLabel = DICTIONARIES.getActiveLabel();
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_DICTIONARY),
                                      activeLabel.empty() ? nullptr : activeLabel.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int activeIndex = DICTIONARIES.getActiveIndex();

  auto textSizeLabel = []() -> const char* {
    switch (DICTIONARIES.getDefinitionTextSize()) {
      case DictionaryStore::DEF_TEXT_SMALL:
        return tr(STR_SMALL);
      case DictionaryStore::DEF_TEXT_LARGE:
        return tr(STR_LARGE);
      default:
        return tr(STR_SMALL);
    }
  };

  if (entries.empty()) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, metrics.listRowHeight * DICTIONARY_ACTION_COUNT},
        DICTIONARY_ACTION_COUNT, selectedIndex,
        [](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(tr(STR_DEFINITION_TEXT_SIZE));
          return std::string(tr(STR_CLEAR_HISTORY));
        },
        nullptr, nullptr,
        [&textSizeLabel](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(textSizeLabel());
          return std::string();
        },
        true);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + metrics.listRowHeight * DICTIONARY_ACTION_COUNT + 22,
                              tr(STR_NO_DICTIONARIES));
    renderer.drawCenteredText(SMALL_FONT_ID, contentTop + metrics.listRowHeight * DICTIONARY_ACTION_COUNT + 48,
                              "/dictionaries/<language>/");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        static_cast<int>(entries.size()) + DICTIONARY_ACTION_COUNT, selectedIndex,
        [&entries](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(tr(STR_DEFINITION_TEXT_SIZE));
          if (index == ACTION_CLEAR_HISTORY) return std::string(tr(STR_CLEAR_HISTORY));
          return entries[index - DICTIONARY_ACTION_COUNT].languageId;
        },
        [&entries](int index) {
          if (index < DICTIONARY_ACTION_COUNT) return std::string();
          return entries[index - DICTIONARY_ACTION_COUNT].name;
        },
        nullptr,
        [&entries, activeIndex, &textSizeLabel](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(textSizeLabel());
          if (index == ACTION_CLEAR_HISTORY) return std::string();
          const int entryIndex = index - DICTIONARY_ACTION_COUNT;
          if (entries[entryIndex].compressed) return std::string("ZIP");
          if (entries[entryIndex].missingFiles) return std::string("!");
          return entryIndex == activeIndex ? std::string(tr(STR_DICTIONARY_ACTIVE)) : std::string();
        },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
