#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ViewportAdapter.h"

template <typename Offset>
concept AcceptedOffset = requires(UiListActivity& activity, UiScreen& screen, fui::ListProps& props, Offset offset) {
  activity.syncListViewport(screen, props, offset);
};
static_assert(AcceptedOffset<int>);
static_assert(!AcceptedOffset<bool>, "Legacy hasSubtitle must not turn into a selection offset");

namespace {
class Target : public fui::DrawTarget {
 public:
  std::string highlightedLabel;
  fui::Rect clip{0, 0, 480, 800};
  int16_t textHeight = 24;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(strlen(text) * textHeight / 2), textHeight};
  }
  int16_t lineHeight(fui::FontId) const override { return textHeight; }
  fui::Rect clipRect() const override { return clip; }
  bool setClipRect(fui::Rect rect) override {
    clip = rect;
    return true;
  }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
  void text(fui::Rect, const char* label, fui::TextStyle style) override {
    if (strncmp(label, "row-", 4) == 0 && (style.color == fui::Color::White || style.inverted))
      highlightedLabel = label;
  }
};

struct Harness {
  UiListActivity activity;
  Target target;
  fui::DeviceContext device;
  fui::ThemeTokens theme = fui::themeTokensForLineHeight(24);
  fui::InteractionBuffer<32> interactions;
  fui::InputSnapshot noInput;
  std::vector<std::string> names;
  std::vector<fui::ListItem> items;
  fui::ListProps props;

  Harness(int count, int height, bool subtitles) {
    device.width = 480;
    device.height = static_cast<int16_t>(height);
    activity.count = count;
    names.reserve(count);
    items.resize(count);
    for (int i = 0; i < count; ++i) names.push_back("row-" + std::to_string(i));
    for (int i = 0; i < count; ++i) {
      items[i].label = names[i].c_str();
      items[i].subtitle = subtitles ? "A subtitle that grows the row" : nullptr;
      items[i].actionValue = static_cast<int16_t>(i);
    }
    props.items = items.data();
    props.count = static_cast<uint16_t>(count);
    props.action = 1;
    props.inputMask = fui::InputTouch;
    theme.listSelectionStyle = fui::SelectionStyle::InvertFill;
    activity.nav.reset();
  }

  void draw(int offset = 0, bool fitHeight = false) {
    for (int pass = 0; pass < 8; ++pass) {
      target.highlightedLabel.clear();
      interactions.beginPublishCycle();
      fui::Frame<32> frame(target, device, noInput, interactions);
      UiScreen screen(frame, theme);
      activity.syncListViewport(screen, props, offset);
      screen.list(props, fitHeight ? activity.measureActionListHeight(screen, props) : 0);
      interactions.publish();
      if (!activity.nav.consumeRebuildNeeded()) return;
    }
    FAIL() << "List viewport did not converge";
  }
};
}  // namespace

TEST(ListNavigation, EveryRowIncludingLastHighlightsTheSameIndexUsedByConfirm) {
  for (bool subtitles : {false, true}) {
    for (int count : {1, 2, 5, 15, 40}) {
      for (int height : {240, 640}) {
        Harness h(count, height, subtitles);
        for (int index = 0; index < count; ++index) {
          h.activity.nav.requestSelection(index);
          h.draw();
          ASSERT_EQ(h.props.selectedIndex, index);
          ASSERT_EQ(h.target.highlightedLabel, h.names[index]);
          ASSERT_EQ(h.activity.nav.selected.load(), index);
        }
      }
    }
  }
}

TEST(ListNavigation, TabsReserveExactlyOneFocusPosition) {
  Harness h(12, 300, true);
  h.draw(1);
  EXPECT_EQ(h.props.selectedIndex, -1);
  EXPECT_TRUE(h.target.highlightedLabel.empty());
  for (int row = 0; row < 12; ++row) {
    h.activity.nav.requestSelection(row + 1);
    h.draw(1);
    EXPECT_EQ(h.props.selectedIndex, row);
    EXPECT_EQ(h.target.highlightedLabel, h.names[row]);
  }
}

TEST(ListNavigation, TouchHitOnHighlightedRowDispatchesTheSameIndex) {
  Harness h(18, 300, true);
  for (int row : {0, 5, 17}) {
    h.activity.nav.requestSelection(row);
    h.draw();
    const auto* hits = h.interactions.publishedData();
    bool found = false;
    for (size_t i = 0; i < h.interactions.publishedCount(); ++i) {
      const auto& hit = hits[i];
      if (hit.value != row) continue;
      fui::InputSnapshot press;
      press.touchPressed = true;
      press.touchX = static_cast<int16_t>(hit.rect.x + hit.rect.width / 2);
      press.touchY = static_cast<int16_t>(hit.rect.y + hit.rect.height / 2);
      h.interactions.routePublished(press);
      press.touchPressed = false;
      press.touchReleased = true;
      const auto event = h.interactions.routePublished(press);
      EXPECT_EQ(event.value, row);
      EXPECT_EQ(event.action, 1);
      found = true;
      break;
    }
    EXPECT_TRUE(found);
  }
}

TEST(ListNavigation, ShrinkingListClampsSelectionWithoutShiftingIt) {
  Harness h(15, 300, true);
  h.activity.nav.requestSelection(14);
  h.draw();
  h.activity.count = h.props.count = 3;
  h.activity.nav.requestSelection(14);
  h.draw();
  EXPECT_EQ(h.activity.nav.selected.load(), 2);
  EXPECT_EQ(h.props.selectedIndex, 2);
  EXPECT_EQ(h.target.highlightedLabel, "row-2");
}

TEST(ListNavigation, SwipeAndButtonFollowKeepTheirOwnSelection) {
  Harness h(20, 300, true);
  h.draw();
  h.activity.nav.requestScroll(5);
  h.draw();
  EXPECT_EQ(h.activity.nav.selected.load(), 0);
  EXPECT_TRUE(h.target.highlightedLabel.empty());
  h.activity.nav.requestSelection(1);
  h.draw();
  EXPECT_EQ(h.target.highlightedLabel, "row-1");
}

TEST(ListNavigation, ShortActionListFitsSubtitlesAndWrappedLabels) {
  for (int textHeight : {24, 36}) {
    Harness h(5, 640, true);
    h.target.textHeight = static_cast<int16_t>(textHeight);
    h.props.labelText.maxLines = 2;
    for (int inset : {0, 20}) {
      h.props.rowInset = static_cast<int16_t>(inset);
      h.items[2].label = "row-with a long label that wraps onto two lines";
      h.draw(0, true);
      EXPECT_EQ(h.activity.nav.drawnRows, 5);
      EXPECT_EQ(h.activity.nav.top, 0);
      h.activity.nav.requestSelection(4);
      h.draw(0, true);
      EXPECT_EQ(h.target.highlightedLabel, "row-4");
      EXPECT_EQ(h.activity.nav.drawnRows, 5);
    }
  }
}

TEST(ListNavigation, ShortActionListClampsToSmallScreenAndScrolls) {
  Harness h(5, 180, true);
  for (int row = 0; row < 5; ++row) {
    h.activity.nav.requestSelection(row);
    h.draw(0, true);
    EXPECT_EQ(h.target.highlightedLabel, h.names[row]);
    EXPECT_LT(h.activity.nav.drawnRows, 5);
  }
}
