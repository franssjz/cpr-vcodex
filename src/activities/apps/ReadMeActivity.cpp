#include "ReadMeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> splitLinesPreserveEmpty(const std::string& value) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t end = value.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(value.substr(start));
      break;
    }
    lines.push_back(value.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}
}  // namespace

int ReadMeActivity::getTopicCount() { return static_cast<int>(Topic::Count); }

std::string ReadMeActivity::getTopicTitle(const Topic topic) {
  switch (topic) {
    case Topic::SyncDay:
      return tr(STR_SYNC_DAY);
    case Topic::Stats:
      return tr(STR_READING_STATS);
    case Topic::Bookmarks:
      return tr(STR_BOOKMARKS);
    case Topic::Sleep:
      return tr(STR_SLEEP);
    case Topic::Shortcuts:
      return tr(STR_README_TOPIC_SHORTCUTS);
    case Topic::Achievements:
      return tr(STR_ACHIEVEMENTS);
    case Topic::IfFound:
      return tr(STR_IF_FOUND_RETURN_ME);
  }
  return "";
}

std::string ReadMeActivity::getTopicBody(const Topic topic) {
  switch (topic) {
    case Topic::SyncDay:
      return tr(STR_README_SYNC_DAY_BODY);
    case Topic::Stats:
      return tr(STR_README_STATS_BODY);
    case Topic::Bookmarks:
      return tr(STR_README_BOOKMARKS_BODY);
    case Topic::Sleep:
      return tr(STR_README_SLEEP_BODY);
    case Topic::Shortcuts:
      return tr(STR_README_SHORTCUTS_BODY);
    case Topic::Achievements:
      return tr(STR_README_ACHIEVEMENTS_BODY);
    case Topic::IfFound:
      return tr(STR_README_IF_FOUND_BODY);
  }
  return "";
}

std::string ReadMeActivity::getTopicIndexLabel(const Topic topic) {
  return std::to_string(static_cast<int>(topic)) + ". " + getTopicTitle(topic);
}

void ReadMeActivity::loadDetailLines() {
  detailLines.clear();
  scrollOffset = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int textWidth =
      renderer.getScreenWidth() - metrics.contentSidePadding * 2 - metrics.scrollBarWidth - metrics.scrollBarRightOffset - 8;

  for (const auto& rawLine : splitLinesPreserveEmpty(getTopicBody(activeTopic))) {
    if (trim(rawLine).empty()) {
      detailLines.emplace_back("");
      continue;
    }

    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, rawLine.c_str(), textWidth, 64, EpdFontFamily::REGULAR);
    if (wrapped.empty()) {
      detailLines.push_back(rawLine);
      continue;
    }
    detailLines.insert(detailLines.end(), wrapped.begin(), wrapped.end());
  }

  while (!detailLines.empty() && detailLines.back().empty()) {
    detailLines.pop_back();
  }
}

int ReadMeActivity::getVisibleDetailLineCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return std::max(1, viewportHeight / lineHeight);
}

int ReadMeActivity::getMaxScrollOffset() const {
  return std::max(0, static_cast<int>(detailLines.size()) - getVisibleDetailLineCount());
}

void ReadMeActivity::openSelectedTopic() {
  if (selectedIndex < 0 || selectedIndex >= getTopicCount()) {
    return;
  }

  activeTopic = static_cast<Topic>(selectedIndex);
  viewMode = ViewMode::Detail;
  loadDetailLines();
  requestUpdate();
}

void ReadMeActivity::onEnter() {
  Activity::onEnter();
  viewMode = ViewMode::Menu;
  selectedIndex = std::clamp(selectedIndex, 0, getTopicCount() - 1);
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void ReadMeActivity::loop() {
  if (viewMode == ViewMode::Menu) {
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
      openSelectedTopic();
      return;
    }

    buttonNavigator.onNextRelease([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, getTopicCount());
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, getTopicCount());
      requestUpdate();
    });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    viewMode = ViewMode::Menu;
    requestUpdate();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right, MappedInputManager::Button::Down}, [this] {
    const int maxScrollOffset = getMaxScrollOffset();
    if (maxScrollOffset <= 0) {
      return;
    }
    scrollOffset = std::min(scrollOffset + 1, maxScrollOffset);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left, MappedInputManager::Button::Up}, [this] {
    if (scrollOffset <= 0) {
      return;
    }
    scrollOffset = std::max(0, scrollOffset - 1);
    requestUpdate();
  });
}

void ReadMeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (viewMode == ViewMode::Menu) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_README),
                   tr(STR_README_GITHUB_HINT));

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, getTopicCount(), selectedIndex,
                 [](const int index) { return getTopicIndexLabel(static_cast<Topic>(index)); }, nullptr,
                 [](const int) { return UIIcon::Text; });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_README),
                 getTopicIndexLabel(activeTopic).c_str());

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleLines = std::max(1, viewportHeight / lineHeight);
  const int endLine = std::min(static_cast<int>(detailLines.size()), scrollOffset + visibleLines);

  int textY = contentTop;
  for (int i = scrollOffset; i < endLine; ++i) {
    if (!detailLines[i].empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, textY, detailLines[i].c_str(), true,
                        EpdFontFamily::REGULAR);
    }
    textY += lineHeight;
  }

  if (static_cast<int>(detailLines.size()) > visibleLines) {
    const int scrollTrackX = pageWidth - metrics.scrollBarRightOffset;
    const int maxScrollOffset = getMaxScrollOffset();
    const int scrollBarHeight = std::max(18, (viewportHeight * visibleLines) / static_cast<int>(detailLines.size()));
    const int scrollBarY =
        contentTop + ((viewportHeight - scrollBarHeight) * std::min(scrollOffset, maxScrollOffset)) / maxScrollOffset;
    renderer.drawLine(scrollTrackX, contentTop, scrollTrackX, contentTop + viewportHeight, true);
    renderer.fillRect(scrollTrackX - metrics.scrollBarWidth + 1, scrollBarY, metrics.scrollBarWidth, scrollBarHeight,
                      true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
