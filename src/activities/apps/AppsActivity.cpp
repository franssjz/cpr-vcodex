#include "AppsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include "ReadingStatsStore.h"
#include "BookmarksAppActivity.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingStatsActivity.h"
#include "ReadingTimelineActivity.h"
#include "SleepAppActivity.h"
#include "SyncDayActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"

namespace {
std::string formatDurationHmCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string getStatsShortcutSubtitle() {
  const std::string todayValue = formatDurationHmCompact(READING_STATS.getTodayReadingMs());
  const std::string goalValue = formatDurationHmCompact(DAILY_READING_GOAL_MS);
  return todayValue + " / " + goalValue + " | " + std::to_string(READING_STATS.getCurrentStreakDays());
}

std::string getShortcutSubtitle(const ShortcutDefinition& definition) {
  if (definition.id == ShortcutId::Stats) {
    return getStatsShortcutSubtitle();
  }
  if (definition.descriptionId == StrId::STR_NONE_OPT) {
    return "";
  }
  return I18N.get(definition.descriptionId);
}
}  // namespace

void AppsActivity::onEnter() {
  Activity::onEnter();
  appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
  selectedIndex = 0;
  requestUpdate();
}

void AppsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSelectedApp();
    return;
  }

  buttonNavigator.onNext([this] {
    if (appShortcuts.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    if (appShortcuts.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
    requestUpdate();
  });
}

void AppsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_APPS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  if (appShortcuts.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(appShortcuts.size()),
                 selectedIndex,
                 [this](const int index) { return std::string(I18N.get(appShortcuts[index]->nameId)); },
                 [this](const int index) { return getShortcutSubtitle(*appShortcuts[index]); },
                 [this](const int index) { return appShortcuts[index]->icon; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void AppsActivity::openSelectedApp() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(appShortcuts.size())) {
    return;
  }

  std::unique_ptr<Activity> activity;
  switch (appShortcuts[selectedIndex]->id) {
    case ShortcutId::BrowseFiles:
      activityManager.goToFileBrowser();
      return;
    case ShortcutId::Stats:
    case ShortcutId::ReadingStats:
      activity = std::make_unique<ReadingStatsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::SyncDay:
      activity = std::make_unique<SyncDayActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Settings:
      activityManager.goToSettings();
      return;
    case ShortcutId::ReadingHeatmap:
      activity = std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput);
      break;
    case ShortcutId::ReadingTimeline:
      activity = std::make_unique<ReadingTimelineActivity>(renderer, mappedInput);
      break;
    case ShortcutId::RecentBooks:
      activityManager.goToRecentBooks();
      return;
    case ShortcutId::Bookmarks:
      activity = std::make_unique<BookmarksAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::FileTransfer:
      activityManager.goToFileTransfer();
      return;
    case ShortcutId::Sleep:
      activity = std::make_unique<SleepAppActivity>(renderer, mappedInput);
      break;
  }

  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
    if (!appShortcuts.empty()) {
      selectedIndex = std::min(selectedIndex, static_cast<int>(appShortcuts.size()) - 1);
    } else {
      selectedIndex = 0;
    }
    requestUpdate();
  });
}
