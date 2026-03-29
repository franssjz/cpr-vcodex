#include "ShortcutOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string getEntryTitle(const ShortcutOrderEntry& entry) {
  return entry.isAppsHub ? std::string(tr(STR_APPS)) : std::string(I18N.get(entry.definition->nameId));
}
}  // namespace

void ShortcutOrderActivity::onEnter() {
  Activity::onEnter();
  reloadEntries();
  requestUpdate();
}

void ShortcutOrderActivity::reloadEntries() {
  entries = getShortcutOrderEntries(group);
  if (entries.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(entries.size()) - 1);
  }
}

void ShortcutOrderActivity::moveSelectedEntry(const int delta) {
  const int targetIndex = selectedIndex + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries.size()) || targetIndex == selectedIndex) {
    return;
  }

  auto& selectedOrder = getShortcutOrderRef(SETTINGS, entries[selectedIndex]);
  auto& targetOrder = getShortcutOrderRef(SETTINGS, entries[targetIndex]);
  std::swap(selectedOrder, targetOrder);
  normalizeShortcutOrderSettings(SETTINGS);
  SETTINGS.saveToFile();

  selectedIndex = targetIndex;
  reloadEntries();
  requestUpdate();
}

const char* ShortcutOrderActivity::getTitle() const {
  return group == ShortcutOrderGroup::Home ? tr(STR_ORDER_HOME_SHORTCUTS) : tr(STR_ORDER_APPS_SHORTCUTS);
}

void ShortcutOrderActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (moveMode) {
      moveMode = false;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      finish();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (!entries.empty()) {
      moveMode = !moveMode;
      SETTINGS.saveToFile();
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) {
      return;
    }
    if (moveMode) {
      moveSelectedEntry(1);
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) {
      return;
    }
    if (moveMode) {
      moveSelectedEntry(-1);
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void ShortcutOrderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, getTitle());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) { return getEntryTitle(entries[index]); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), moveMode ? tr(STR_DONE) : tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
