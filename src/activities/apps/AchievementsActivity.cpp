#include "AchievementsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int ROW_HEIGHT = 56;
constexpr int ICON_BOX_SIZE = 24;

std::string formatDurationCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string getProgressLabel(const AchievementView& view) {
  if (view.state.unlocked) {
    return tr(STR_DONE);
  }

  switch (view.definition->metric) {
    case AchievementMetric::TotalReadingMs:
    case AchievementMetric::MaxSessionMs:
      return formatDurationCompact(view.progress) + " / " + formatDurationCompact(view.target);
    default:
      return std::to_string(view.progress) + " / " + std::to_string(view.target);
  }
}

const char* tabLabel(const bool completed) { return completed ? tr(STR_COMPLETED) : tr(STR_PENDING); }

std::string tabLabelWithCount(const bool completed, const int count) {
  return std::string(tabLabel(completed)) + " (" + std::to_string(count) + ")";
}

const char* emptyStateLabel(const bool completed) {
  return completed ? tr(STR_NO_COMPLETED_ACHIEVEMENTS) : tr(STR_NO_PENDING_ACHIEVEMENTS);
}

void drawLockIcon(const GfxRenderer& renderer, const int x, const int y, const bool inverted) {
  renderer.drawRect(x + 5, y + 9, 14, 11, inverted);
  renderer.drawLine(x + 8, y + 9, x + 8, y + 6, inverted);
  renderer.drawLine(x + 15, y + 9, x + 15, y + 6, inverted);
  renderer.drawLine(x + 8, y + 6, x + 10, y + 4, inverted);
  renderer.drawLine(x + 10, y + 4, x + 13, y + 4, inverted);
  renderer.drawLine(x + 13, y + 4, x + 15, y + 6, inverted);
  renderer.fillRect(x + 11, y + 13, 2, 4, inverted);
}

void drawTrophyIcon(const GfxRenderer& renderer, const int x, const int y, const bool inverted) {
  renderer.drawRect(x + 7, y + 4, 10, 8, inverted);
  renderer.drawLine(x + 7, y + 6, x + 5, y + 6, inverted);
  renderer.drawLine(x + 5, y + 6, x + 5, y + 10, inverted);
  renderer.drawLine(x + 17, y + 6, x + 19, y + 6, inverted);
  renderer.drawLine(x + 19, y + 6, x + 19, y + 10, inverted);
  renderer.drawLine(x + 10, y + 12, x + 10, y + 16, inverted);
  renderer.drawLine(x + 14, y + 12, x + 14, y + 16, inverted);
  renderer.drawLine(x + 8, y + 18, x + 16, y + 18, inverted);
  renderer.drawLine(x + 9, y + 16, x + 15, y + 16, inverted);
}
}  // namespace

void AchievementsActivity::rebuildVisibleIndexes() {
  visibleIndexes.clear();
  const bool showCompleted = selectedTab == FilterTab::Completed;

  for (int i = 0; i < static_cast<int>(achievements.size()); ++i) {
    if (achievements[i].state.unlocked == showCompleted) {
      visibleIndexes.push_back(i);
    }
  }

  if (visibleIndexes.empty() && !achievements.empty()) {
    selectedTab = showCompleted ? FilterTab::Pending : FilterTab::Completed;
    const bool fallbackCompleted = selectedTab == FilterTab::Completed;
    for (int i = 0; i < static_cast<int>(achievements.size()); ++i) {
      if (achievements[i].state.unlocked == fallbackCompleted) {
        visibleIndexes.push_back(i);
      }
    }
  }

  if (selectedIndex >= static_cast<int>(visibleIndexes.size())) {
    selectedIndex = std::max(0, static_cast<int>(visibleIndexes.size()) - 1);
  }
}

void AchievementsActivity::refreshEntries() {
  ACHIEVEMENTS.reconcileFromCurrentStats();
  achievements = ACHIEVEMENTS.buildViews();
  rebuildVisibleIndexes();
}

void AchievementsActivity::onEnter() {
  Activity::onEnter();
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  refreshEntries();
  requestUpdate();
}

void AchievementsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectedTab = (selectedTab == FilterTab::Pending) ? FilterTab::Completed : FilterTab::Pending;
    selectedIndex = 0;
    rebuildVisibleIndexes();
    requestUpdate();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right, MappedInputManager::Button::Down}, [this] {
    if (visibleIndexes.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(visibleIndexes.size()));
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left, MappedInputManager::Button::Up}, [this] {
    if (visibleIndexes.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(visibleIndexes.size()));
    requestUpdate();
  });
}

void AchievementsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int tabY = metrics.topPadding + metrics.headerHeight;
  const int contentTop = tabY + metrics.tabBarHeight + metrics.verticalSpacing;
  const int viewportHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int sidePadding = metrics.contentSidePadding;
  const int rowWidth = pageWidth - sidePadding * 2;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ACHIEVEMENTS));

  int pendingCount = 0;
  int completedCount = 0;
  for (const auto& achievement : achievements) {
    if (achievement.state.unlocked) {
      ++completedCount;
    } else {
      ++pendingCount;
    }
  }

  const std::string pendingTab = tabLabelWithCount(false, pendingCount);
  const std::string completedTab = tabLabelWithCount(true, completedCount);
  const std::vector<TabInfo> tabs = {
      {pendingTab.c_str(), selectedTab == FilterTab::Pending},
      {completedTab.c_str(), selectedTab == FilterTab::Completed},
  };
  GUI.drawTabBar(renderer, Rect{0, tabY, pageWidth, metrics.tabBarHeight}, tabs, false);

  if (visibleIndexes.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop + viewportHeight / 2 - 12,
                              emptyStateLabel(selectedTab == FilterTab::Completed));
  } else {
    int firstVisibleIndex = 0;
    const int maxVisibleRows = std::max(1, viewportHeight / ROW_HEIGHT);
    if (selectedIndex >= maxVisibleRows) {
      firstVisibleIndex = selectedIndex - maxVisibleRows + 1;
    }

    int currentY = contentTop;
    for (int visibleIndex = firstVisibleIndex; visibleIndex < static_cast<int>(visibleIndexes.size()); ++visibleIndex) {
      if (currentY + ROW_HEIGHT > contentTop + viewportHeight) {
        break;
      }

      const auto& entry = achievements[visibleIndexes[visibleIndex]];
      const bool selected = visibleIndex == selectedIndex;
      const Rect rowRect{sidePadding, currentY, rowWidth, ROW_HEIGHT - 4};
      if (selected) {
        renderer.fillRectDither(rowRect.x, rowRect.y, rowRect.width, rowRect.height, Color::LightGray);
        renderer.drawRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height);
      }

      const int iconX = rowRect.x + 10;
      const int iconY = rowRect.y + (rowRect.height - ICON_BOX_SIZE) / 2;
      if (entry.state.unlocked) {
        drawTrophyIcon(renderer, iconX, iconY, true);
      } else {
        drawLockIcon(renderer, iconX, iconY, true);
      }

      const std::string title = ACHIEVEMENTS.getTitle(entry.definition->id);
      const std::string description = ACHIEVEMENTS.getDescription(entry.definition->id);
      const std::string progress = getProgressLabel(entry);
      const int progressWidth = renderer.getTextWidth(UI_10_FONT_ID, progress.c_str(), EpdFontFamily::REGULAR);
      const int textX = iconX + ICON_BOX_SIZE + 12;
      const int textWidth = rowRect.width - (textX - rowRect.x) - progressWidth - 18;

      const std::string truncatedTitle =
          renderer.truncatedText(UI_10_FONT_ID, title.c_str(), textWidth, EpdFontFamily::BOLD);
      const std::string truncatedDescription =
          renderer.truncatedText(SMALL_FONT_ID, description.c_str(), textWidth, EpdFontFamily::REGULAR);

      renderer.drawText(UI_10_FONT_ID, textX, rowRect.y + 8, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, textX, rowRect.y + 29, truncatedDescription.c_str(), true,
                        EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, rowRect.x + rowRect.width - progressWidth - 10, rowRect.y + 18, progress.c_str(),
                        true, EpdFontFamily::REGULAR);

      currentY += ROW_HEIGHT;
    }
  }

  const std::string nextTabLabel = tabLabel(selectedTab == FilterTab::Pending);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), nextTabLabel.c_str(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
