#include "SleepAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "SleepPreviewActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/SleepImageUtils.h"

void SleepAppActivity::loadDirectories() {
  directories = SleepImageUtils::listSleepDirectories();
  const int itemCount = static_cast<int>(directories.size()) + 1;
  if (selectedIndex >= itemCount) {
    selectedIndex = std::max(0, itemCount - 1);
  }
}

void SleepAppActivity::onEnter() {
  Activity::onEnter();
  loadDirectories();
  requestUpdate();
}

void SleepAppActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      SETTINGS.sleepImageOrder = (SETTINGS.sleepImageOrder + 1) % CrossPointSettings::SLEEP_IMAGE_ORDER_COUNT;
      SETTINGS.saveToFile();
      requestUpdate();
    } else {
      openSelectedDirectory();
    }
    return;
  }

  const int itemCount = static_cast<int>(directories.size()) + 1;
  if (itemCount == 0) {
    return;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void SleepAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const std::string selectedDirectory = SleepImageUtils::resolveConfiguredSleepDirectory();
  const char* sleepOrderLabel =
      SETTINGS.sleepImageOrder == CrossPointSettings::SLEEP_IMAGE_SHUFFLE ? tr(STR_SHUFFLE) : tr(STR_SEQUENTIAL);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SLEEP));

  if (directories.empty()) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, 1, selectedIndex,
        [](int) { return std::string(tr(STR_SLEEP_ORDER)); }, nullptr, [](int) { return UIIcon::Recent; },
        [sleepOrderLabel](int) { return std::string(sleepOrderLabel); }, true);

    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 56, tr(STR_NO_SLEEP_DIRECTORIES));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(directories.size()) + 1,
        selectedIndex,
        [this](int index) {
          if (index == 0) return std::string(tr(STR_SLEEP_ORDER));
          return SleepImageUtils::getDirectoryLabel(directories[index - 1]);
        },
        nullptr, [](int index) { return index == 0 ? UIIcon::Recent : UIIcon::Folder; },
        [&selectedDirectory, sleepOrderLabel, this](int index) {
          if (index == 0) {
            return std::string(sleepOrderLabel);
          }
          return directories[index - 1] == selectedDirectory ? std::string(tr(STR_SELECTED)) : std::string();
        },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), selectedIndex == 0 ? tr(STR_SELECT) : tr(STR_OPEN),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SleepAppActivity::openSelectedDirectory() {
  if (directories.empty() || selectedIndex == 0) {
    return;
  }

  startActivityForResult(std::make_unique<SleepPreviewActivity>(renderer, mappedInput, directories[selectedIndex - 1]),
                         [this](const ActivityResult&) {
                           loadDirectories();
                           requestUpdate();
                         });
}
