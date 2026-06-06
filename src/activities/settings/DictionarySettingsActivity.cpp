#include "DictionarySettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "DictionaryStore.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

void DictionarySettingsActivity::onEnter() {
  Activity::onEnter();
  DICT_STORE.scan();
  DICT_STORE.loadConfig();
  DICT_STORE.syncCheckpointsToEnabled();
  totalItems = static_cast<int>(DICT_STORE.getEntries().size());
  selectedIndex = 0;
  requestUpdate();
}

void DictionarySettingsActivity::onExit() {
  DICT_STORE.saveConfig();
  Activity::onExit();
}

void DictionarySettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (totalItems > 0) {
      const auto& entry = DICT_STORE.getEntries()[selectedIndex];
      if (entry.compressed) {
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                   "Cannot enable",
                                                   "Unzip the .dict.dz file to a plain .dict file first (use 7-Zip)"),
            [this](const ActivityResult&) { requestUpdate(); });
      } else {
        DICT_STORE.setEnabled(selectedIndex, !entry.enabled);
        requestUpdate();
      }
    }
    return;
  }

  // Left = move selected dict up in order
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selectedIndex > 0) {
      DICT_STORE.moveUp(selectedIndex);
      selectedIndex--;
      requestUpdate();
    }
    return;
  }

  // Right = move selected dict down in order
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (selectedIndex < totalItems - 1) {
      DICT_STORE.moveDown(selectedIndex);
      selectedIndex++;
      requestUpdate();
    }
    return;
  }

  // Left/Right are reorder controls on this screen. They are also part of the
  // navigator's previous/next button sets ({Up, Left} and {Down, Right}), so
  // their *release* would otherwise drive list navigation a second time after
  // the reorder already moved the cursor on press. Swallow those releases here.
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    return;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator.onNextRelease([this] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
      requestUpdate();
    }
  });

  buttonNavigator.onPreviousRelease([this] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
      requestUpdate();
    }
  });

  buttonNavigator.onNextContinuous([this, pageItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
      requestUpdate();
    }
  });

  buttonNavigator.onPreviousContinuous([this, pageItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
      requestUpdate();
    }
  });
}

void DictionarySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_MANAGE_DICTS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (totalItems == 0) {
    const int centerY = contentTop + contentHeight / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NO_DICTS_FOUND));
  } else {
    const auto& entries = DICT_STORE.getEntries();
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectedIndex,
        [&entries](int index) { return entries[index].name.c_str(); },
        [&entries](int index) -> std::string {
          return entries[index].compressed ? "Unzip required" : "";
        },
        nullptr,
        [&entries](int index) { return entries[index].enabled ? "[ON]" : "[OFF]"; },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
