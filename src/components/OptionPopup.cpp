#include "OptionPopup.h"

#include <components/text/text-area.h>

#include "activities/RenderLock.h"

namespace fui = freeink::ui;

namespace {
// The SDK text-area walker has no line-count cap, unlike dialog text slots.
int wrappedHeight(const fui::DrawTarget& target, int width, const std::string& text, fui::TextStyle style) {
  if (text.empty()) return 0;
  return fui::textAreaMeasure(target, width, text.c_str(), style, 0).lineCount * target.lineHeight(style.font);
}

void drawWrapped(fui::DrawTarget& target, fui::Rect bounds, int y, const std::string& text, fui::TextStyle style) {
  const int lh = target.lineHeight(style.font);
  char line[224];
  fui::textAreaWalk(target, bounds.width, text.c_str(), style, [&](uint32_t index, const fui::TextAreaLine& span) {
    const int lineY = y + static_cast<int>(index) * lh;
    if (lineY + lh <= bounds.y || lineY >= bounds.bottom()) return;
    const auto size = std::min<size_t>(span.len, sizeof(line) - 1);
    memcpy(line, text.c_str() + span.start, size);
    line[size] = '\0';
    target.text(fui::Rect{bounds.x, fui::clampI16(lineY), bounds.width, fui::clampI16(lh)}, line, style);
  });
}
}  // namespace

void OptionPopup::activate(int currentIndex, std::function<void(int)> onSelect) {
  const int count = static_cast<int>(ownedStrings.size());
  selectedIndex = currentIndex >= 0 && currentIndex < count ? currentIndex : 0;
  onSelectCallback = std::move(onSelect);
  headline.clear();
  message.clear();
  rows.resize(count + 3);
  viewport = {};
  firstRender = true;
  uiReady = false;
  active = count > 0;
}

bool OptionPopup::handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
  if (!active) return false;
  bool changed = false;
  bool selected = false;
  {
    // Serialize layout feedback with input, but never hold the render lock
    // across a host callback/repaint (reader overlays acquire their own lock).
    RenderLock lock;
    const int count = static_cast<int>(ownedStrings.size());
    const auto snap = touchSnapshotFrom(input);
    const auto swipe = input.wasSwipe();
    if (uiReady && (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down)) {
      interactions.routePublished(snap);
      viewport.scroll(swipe == MappedInputManager::SwipeDir::Up ? viewport.pageStep() : -viewport.pageStep());
      changed = true;
    } else if (snap.touchPressed || snap.touchReleased || snap.touchHeld) {
      if (uiReady) {
        const auto event = interactions.routePublished(snap);
        if (event && event.action == ACTION_OPTION) {
          selectedIndex = event.value;
          selected = changed = true;
          active = false;
          haptic_feedback::touchAction();
        } else if (!event && snap.touchReleased && snap.touchX >= 0) {
          active = false;
          changed = true;
          haptic_feedback::touchAction();
        } else if (snap.touchPressed) {
          const auto index = interactions.activeIndex();
          if (index >= 0) {
            const auto& hit = interactions.publishedData()[index];
            if (hit.action == ACTION_OPTION && selectedIndex != hit.value) {
              selectedIndex = hit.value;
              changed = true;
            }
          }
        }
      }
    } else if (input.wasReleased(MappedInputManager::Button::Back)) {
      active = false;
      changed = true;
    } else if (uiReady && (input.wasPressed(MappedInputManager::Button::NavPrevious) ||
                           input.wasPressed(MappedInputManager::Button::NavNext))) {
      const int direction = input.wasPressed(MappedInputManager::Button::NavNext) ? 1 : -1;
      selectedIndex = viewport.navigate(direction, selectedIndex, rows.data() + 3, count);
      changed = true;
    } else if (uiReady && input.wasReleased(MappedInputManager::Button::Confirm)) {
      // Reveal hidden actions before accepting them on a long message.
      if (viewport.visible(rows[selectedIndex + 3])) {
        active = false;
        selected = true;
      } else {
        viewport.reveal(rows[selectedIndex + 3]);
      }
      changed = true;
    }
  }
  if (selected && onSelectCallback) onSelectCallback(selectedIndex);
  if (changed) requestUpdate();
  return true;
}

bool OptionPopup::processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
  if (!active) return false;
  const auto labels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  render(renderer);
  renderer.displayBuffer();
  return true;
}

void OptionPopup::render(const GfxRenderer& renderer) const {
  if (!active) return;
  auto target = makeUiTarget(renderer);
  const auto& theme = refreshSharedUiThemeTokens(target);
  const auto device = target.deviceContext();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const fui::InputSnapshot noInput{};
  interactions.beginPublishCycle();
  fui::Frame<INTERACTION_CAPACITY> frame(target, device, noInput, interactions);

  // Top/bottom clearance protects button hints in both portrait orientations;
  // the narrower popup width also clears the strip in landscape.
  const int margin = device.hasTouch ? 8 : metrics.buttonHintsHeight + 4;
  const auto safe =
      device.safeRect().inset(fui::Insets{static_cast<int16_t>(margin), 8, static_cast<int16_t>(margin), 8});
  const int width = std::min<int>(safe.width * 3 / 4, safe.width - metrics.optionPopupDialogSideMargin * 2);
  const int padding = std::max(8, metrics.optionPopupInnerPadding);
  const int gap = metrics.optionPopupItemSpacing;
  const int scrollWidth = 4;
  const int contentWidth = std::max(1, width - padding * 2 - scrollWidth - 6);
  fui::TextStyle text = theme.bodyText;
  text.align = fui::TextAlign::Center;
  text.maxLines = 1;
  fui::TextStyle caption = text;
  caption.bold = true;
  const int lineHeight = target.lineHeight(text.font);
  const int verticalPadding = metrics.optionPopupSelectionVPadding;
  int total = 0;
  const std::string* slots[] = {&title, &headline, &message};
  for (int i = 0; i < 3; ++i) {
    rows[i] = {total, wrappedHeight(target, contentWidth, *slots[i], i == 0 ? caption : text)};
    if (rows[i].height > 0) total += rows[i].height + gap;
  }
  for (size_t i = 0; i < ownedStrings.size(); ++i) {
    const int height = std::max(device.hasTouch ? device.minTouchSize : lineHeight,
                                wrappedHeight(target, contentWidth - 8, ownedStrings[i], text) + verticalPadding * 2);
    rows[i + 3] = {total, height};
    total += height + gap;
  }
  total -= gap;
  const int height = std::min<int>(safe.height, total + padding * 2);
  const auto dialog = fui::centeredRect(safe, fui::Size{fui::clampI16(width), fui::clampI16(height)});
  const auto content = dialog.inset(
      fui::Insets{fui::clampI16(padding), fui::clampI16(padding), fui::clampI16(padding), fui::clampI16(padding)});
  viewport.configure(total, content.height, lineHeight);
  if (firstRender) {
    // Long prompts start at the beginning; ordinary pickers reveal the current
    // choice even when it lies beyond the first screenful.
    if (rows[3].top + rows[3].height <= viewport.height) viewport.reveal(rows[selectedIndex + 3]);
    firstRender = false;
  }

  const auto panelStyle = fui::defaultPopupStyles().normal;
  target.fill(dialog, panelStyle.background, metrics.popupCornerRadius);
  target.stroke(dialog, fui::Paint::solid(fui::Color::Black), metrics.popupFrameThickness, metrics.popupCornerRadius);
  frame.hit(dialog, ACTION_CHROME, 0, fui::InputTouch);
  const auto previousClip = target.clipRect();
  target.setClipRect(content);
  const fui::Rect textBounds{content.x, content.y, fui::clampI16(contentWidth), content.height};
  for (int i = 0; i < 3; ++i) {
    if (rows[i].height > 0 && viewport.visible(rows[i]))
      drawWrapped(target, textBounds, content.y + rows[i].top - viewport.offset, *slots[i], i == 0 ? caption : text);
  }

  // Large style tables live off-stack, reused only by the serialized render task.
  static fui::ButtonProps button;
  button = fui::ButtonProps{};
  button.styles = fui::defaultButtonStyles();
  if (theme.listSelectionStyle == fui::SelectionStyle::InvertFill && theme.listRowRadius > 0) {
    button.styles.focused = button.styles.selected;
    fui::setStyleRadius(button.styles, theme.listRowRadius);
  }
  for (size_t i = 0; i < ownedStrings.size(); ++i) {
    const auto& row = rows[i + 3];
    if (!viewport.visible(row)) continue;
    const int y = content.y + row.top - viewport.offset;
    const fui::Rect rect{content.x, fui::clampI16(y), fui::clampI16(contentWidth), fui::clampI16(row.height)};
    button.state = static_cast<int>(i) == selectedIndex ? fui::StateFocused : fui::StateNormal;
    // Paint via the SDK, but register only the visible intersection: clipped
    // rows cannot capture touches on the title, border or button hints.
    fui::button(frame, rect, button);
    const int hitTop = std::max<int>(rect.y, content.y);
    const int hitBottom = std::min<int>(rect.bottom(), content.bottom());
    frame.hit(fui::Rect{rect.x, fui::clampI16(hitTop), rect.width, fui::clampI16(hitBottom - hitTop)}, ACTION_OPTION,
              static_cast<int16_t>(i), fui::InputTouch);
    const auto rowText = fui::textStyleWithForeground(text, button.styles.resolve(button.state).foreground);
    const fui::Rect labelBounds{static_cast<int16_t>(content.x + 4), content.y, static_cast<int16_t>(contentWidth - 8),
                                content.height};
    drawWrapped(target, labelBounds, y + verticalPadding, ownedStrings[i], rowText);
  }
  target.setClipRect(previousClip);
  if (viewport.maximum() > 0) {
    const int trackX = content.right() - scrollWidth;
    const int thumbHeight = std::max(12, viewport.height * viewport.height / viewport.total);
    const int thumbY = content.y + static_cast<int>(static_cast<int64_t>(viewport.offset) *
                                                    (viewport.height - thumbHeight) / viewport.maximum());
    target.fill(fui::Rect{fui::clampI16(trackX + 1), content.y, 1, content.height},
                fui::Paint::dither(fui::Color::LightGray));
    target.fill(fui::Rect{fui::clampI16(trackX), fui::clampI16(thumbY), scrollWidth, fui::clampI16(thumbHeight)},
                fui::Paint::solid(fui::Color::Black));
  }
  interactions.publish();
  uiReady = true;
}
