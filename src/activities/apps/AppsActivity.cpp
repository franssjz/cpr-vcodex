#include "AppsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "AchievementsActivity.h"
#include "BookmarksAppActivity.h"
#include "FavoritesAppActivity.h"
#include "FlashcardsAppActivity.h"
#include "IfFoundActivity.h"
#include "ReadMeActivity.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingProfileActivity.h"
#include "ReadingStatsActivity.h"
#include "SleepAppActivity.h"
#include "SyncDayActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutUiMetadata.h"

namespace {
std::string buildAppsHeaderSubtitle(const int selectedIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0) {
    return "";
  }

  const int safeItemsPerPage = std::max(1, itemsPerPage);
  const int currentPage = std::clamp(selectedIndex, 0, totalItems - 1) / safeItemsPerPage + 1;
  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  return std::to_string(currentPage) + "/" + std::to_string(totalPages) + " | " + std::to_string(totalItems);
}
}  // namespace

void AppsActivity::onEnter() {
  Activity::onEnter();
  appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
  selectedIndex = 0;
  rebuildShortcutSubtitles();
  requestUpdate();
}

void AppsActivity::loop() {
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSelectedApp();
    return;
  }

  buttonNavigator.onNextPress([this] {
    if (appShortcuts.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousPress([this] {
    if (appShortcuts.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, pageItems] {
    if (appShortcuts.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, static_cast<int>(appShortcuts.size()), pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, pageItems] {
    if (appShortcuts.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, static_cast<int>(appShortcuts.size()), pageItems);
    requestUpdate();
  });
}

void AppsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
  const std::string headerSubtitle =
      buildAppsHeaderSubtitle(selectedIndex, static_cast<int>(appShortcuts.size()), pageItems);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_APPS), headerSubtitle.empty() ? nullptr : headerSubtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  if (appShortcuts.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(appShortcuts.size()),
                 selectedIndex,
                 [this](const int index) { return std::string(I18N.get(appShortcuts[index]->nameId)); },
                 [this](const int index) {
                   return (index >= 0 && index < static_cast<int>(shortcutSubtitles.size())) ? shortcutSubtitles[index]
                                                                                              : std::string{};
                 },
                 [this](const int index) { return appShortcuts[index]->icon; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void AppsActivity::rebuildShortcutSubtitles() {
  shortcutSubtitles.clear();
  shortcutSubtitles.reserve(appShortcuts.size());

  for (const ShortcutDefinition* definition : appShortcuts) {
    if (definition == nullptr) {
      shortcutSubtitles.emplace_back();
      continue;
    }
    shortcutSubtitles.push_back(ShortcutUiMetadata::getSubtitle(*definition));
  }
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
    case ShortcutId::ReadingProfile:
      activity = std::make_unique<ReadingProfileActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Achievements:
      activity = std::make_unique<AchievementsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::IfFound:
      activity = std::make_unique<IfFoundActivity>(renderer, mappedInput);
      break;
    case ShortcutId::ReadMe:
      activity = std::make_unique<ReadMeActivity>(renderer, mappedInput);
      break;
    case ShortcutId::RecentBooks:
      activityManager.goToRecentBooks();
      return;
    case ShortcutId::Bookmarks:
      activity = std::make_unique<BookmarksAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Favorites:
      activity = std::make_unique<FavoritesAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Flashcards:
      activity = std::make_unique<FlashcardsAppActivity>(renderer, mappedInput);
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
    rebuildShortcutSubtitles();
    if (!appShortcuts.empty()) {
      selectedIndex = std::min(selectedIndex, static_cast<int>(appShortcuts.size()) - 1);
    } else {
      selectedIndex = 0;
    }
    requestUpdate();
  });
}
