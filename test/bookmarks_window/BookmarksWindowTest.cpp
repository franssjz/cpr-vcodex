#include <gtest/gtest.h>

#include <charconv>
#include <cstring>
#include <set>
#include <type_traits>

#include "BookmarksAdapter.h"

static_assert(std::extent_v<decltype(BookmarksActivity::rowLabels)> == 24);
static_assert(std::extent_v<decltype(BookmarksActivity::rowItems)> == 24);

namespace {
std::vector<BookmarkStore::Bookmark> makeBookmarks(int count) {
  std::vector<BookmarkStore::Bookmark> bookmarks;
  for (int i = 0; i < count; ++i) {
    BookmarkStore::Bookmark bookmark;
    bookmark.spineIndex = static_cast<uint16_t>(i / 8);
    bookmark.pageNumber = static_cast<uint16_t>(i);
    bookmark.hasVisibleTextOffset = true;
    bookmark.visibleTextOffset = static_cast<uint32_t>(i * 100 + 9);
    bookmark.isTextHighlight = true;
    // Dense, wrapped CJK snippets exercise the same label path as issue #227.
    bookmark.snippet = "中文片段 日本語 한국어 ";
    while (bookmark.snippet.size() < 450) bookmark.snippet += "文字内容 ";
    bookmarks.push_back(std::move(bookmark));
  }
  return bookmarks;
}

class Target : public fui::DrawTarget {
 public:
  fui::Rect clip{0, 0, 480, 800};
  int highlightedRow = -1;
  std::set<int> drawnRows;
  std::function<void(const char*)> beforeMeasure;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    if (beforeMeasure) beforeMeasure(text);
    return {static_cast<int16_t>(strlen(text) * 12), 24};
  }
  int16_t lineHeight(fui::FontId) const override { return 24; }
  fui::Rect clipRect() const override { return clip; }
  bool setClipRect(fui::Rect value) override {
    clip = value;
    return true;
  }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
  void text(fui::Rect, const char* text, fui::TextStyle style) override {
    int number = 0;
    const auto parsed = std::from_chars(text, text + strlen(text), number);
    if (parsed.ec != std::errc{} || *parsed.ptr != '.') return;
    drawnRows.insert(number - 1);
    if (style.color == fui::Color::White || style.inverted) highlightedRow = number - 1;
  }
};

struct Harness {
  GfxRenderer renderer;
  MappedInputManager input;
  BookmarksActivity activity;
  Target target;
  fui::DeviceContext device;
  fui::ThemeTokens theme = fui::themeTokensForLineHeight(24);
  fui::InteractionBuffer<32> interactions;
  fui::InputSnapshot noInput;
  int lastBuildPasses = 0;

  explicit Harness(int count = 256, int height = 800)
      : activity(renderer, input, makeBookmarks(count), nullptr, {}, [](const auto&) { return true; }) {
    renderer.windowContext = activity.rowLabels;
    renderer.height = height;
    device.width = 480;
    device.height = static_cast<int16_t>(height);
    target.clip.height = static_cast<int16_t>(height);
    theme.smallText.font = static_cast<fui::FontId>(uiScaleSpec().smallFontId);
    theme.listSelectionStyle = fui::SelectionStyle::InvertFill;
    formattedLabels = 0;
    activity.onEnter();
  }

  void draw() {
    lastBuildPasses = 0;
    for (int pass = 0; pass < 8; ++pass) {
      ++lastBuildPasses;
      target.highlightedRow = -1;
      target.drawnRows.clear();
      interactions.beginPublishCycle();
      fui::Frame<32> frame(target, device, noInput, interactions);
      UiScreen screen(frame, theme);
      activity.buildScreen(screen);
      interactions.publish();
      if (!activity.nav.consumeRebuildNeeded()) return;
    }
    FAIL() << "Bookmark viewport did not converge";
  }

  fui::ActionEvent tapRow(int row) {
    const auto* hits = interactions.publishedData();
    for (size_t i = 0; i < interactions.publishedCount(); ++i) {
      if (hits[i].value != row) continue;
      fui::InputSnapshot touch;
      touch.touchX = static_cast<int16_t>(hits[i].rect.x + hits[i].rect.width / 2);
      touch.touchY = static_cast<int16_t>(hits[i].rect.y + hits[i].rect.height / 2);
      touch.touchPressed = true;
      interactions.routePublished(touch);
      touch.touchPressed = false;
      touch.touchReleased = true;
      return interactions.routePublished(touch);
    }
    ADD_FAILURE() << "No interaction for row " << row;
    return {};
  }
};
}  // namespace

TEST(BookmarksWindow, FirstPaintFormatsAndBatchPrewarmsOnlyTwentyFourLabels) {
  Harness h;
  EXPECT_EQ(h.renderer.cache.clearCalls, 1);
  EXPECT_EQ(formattedLabels, 0);
  EXPECT_TRUE(h.renderer.batches.empty());
  h.draw();
  EXPECT_EQ(h.activity.listCount(), 256);
  EXPECT_EQ(h.activity.rowWindowStart, 0);
  EXPECT_EQ(h.activity.rowWindowCount, 24);
  EXPECT_EQ(formattedLabels, 24);
  ASSERT_EQ(h.renderer.batches.size(), 1u);
  const auto& batch = h.renderer.batches.front();
  EXPECT_EQ(batch.font, uiScaleSpec().smallFontId);
  ASSERT_EQ(batch.labels.size(), 24u);
  for (int i = 0; i < 24; ++i) {
    EXPECT_EQ(batch.labels[i], std::to_string(i + 1) + ". Highlight: " + h.activity.bookmarks[i].snippet);
    EXPECT_EQ(h.activity.rowItems[i].actionValue, i);
    EXPECT_EQ(h.activity.rowItems[i].label, h.activity.rowLabels[i].c_str());
  }
}

TEST(BookmarksWindow, RepaintAndSelectionWithinViewportReuseLabelsAndBatch) {
  Harness h;
  h.draw();
  const char* firstLabel = h.activity.rowItems[0].label;
  for (int row : {1, 0, 1}) {
    h.activity.nav.requestSelection(row);
    h.draw();
    EXPECT_EQ(h.target.highlightedRow, row);
    EXPECT_EQ(h.activity.rowWindowStart, 0);
    EXPECT_EQ(h.activity.rowItems[0].label, firstLabel);
  }
  EXPECT_EQ(formattedLabels, 24);
  EXPECT_EQ(h.renderer.batches.size(), 1u);
}

TEST(BookmarksWindow, WrapToLastRowStartsPrewarmAtViewportAndPreservesActions) {
  Harness h;
  h.draw();
  const size_t batchesBeforeWrap = h.renderer.batches.size();
  h.activity.nav.requestSelection(255);
  h.draw();
  EXPECT_EQ(h.lastBuildPasses, 1);
  EXPECT_EQ(h.renderer.batches.size(), batchesBeforeWrap + 1);
  EXPECT_EQ(h.target.highlightedRow, 255);
  EXPECT_EQ(h.activity.rowWindowStart, h.activity.nav.top);
  EXPECT_GT(h.activity.rowWindowStart, 232);
  EXPECT_EQ(h.activity.rowWindowCount, 256 - h.activity.rowWindowStart);
  ASSERT_FALSE(h.renderer.batches.empty());
  const auto& batch = h.renderer.batches.back();
  EXPECT_EQ(batch.labels.front(), h.activity.rowLabels[0]);
  EXPECT_EQ(batch.labels.back(), "256. Highlight: " + h.activity.bookmarks.back().snippet);
  for (const auto& prewarm : h.renderer.batches) EXPECT_LE(prewarm.labels.size(), 24u);
  const auto event = h.tapRow(255);
  EXPECT_EQ(event.action, UiListActivity::ACTION_ROW);
  EXPECT_EQ(event.value, 255);
  h.activity.activateIndex(event.value);
  EXPECT_EQ(h.activity.bookmarkResult.pageNumber, 255);
  EXPECT_EQ(h.activity.bookmarkResult.visibleTextOffset, 25509u);
  h.activity.finished = false;
  h.input.confirmReleased = true;
  EXPECT_TRUE(h.activity.handleButtons());
  EXPECT_TRUE(h.activity.finished);
  EXPECT_EQ(h.activity.bookmarkResult.pageNumber, 255);
  ASSERT_FALSE(h.renderer.individualPrewarms.empty());
  EXPECT_EQ(h.renderer.individualPrewarms.back(), batch.labels.back());
}

TEST(BookmarksWindow, MeasuredRowHintStillConvergesWhenJumpChangesRowHeight) {
  for (const bool firstHalfShort : {true, false}) {
    for (const int height : {240, 800}) {
      SCOPED_TRACE(::testing::Message() << "firstHalfShort=" << firstHalfShort << " height=" << height);
      Harness h(256, height);
      for (int row = 0; row < 256; ++row) {
        if ((row < 128) == firstHalfShort) h.activity.bookmarks[row].snippet = "short";
      }
      h.draw();
      for (const int selected : {255, 0, 128, 127}) {
        h.activity.nav.requestSelection(selected);
        h.draw();
        EXPECT_EQ(h.target.highlightedRow, selected);
        EXPECT_EQ(h.activity.nav.selected.load(), selected);
        EXPECT_EQ(h.tapRow(selected).value, selected);
        EXPECT_EQ(h.activity.rowWindowStart, h.activity.nav.top);
      }
    }
  }
}

TEST(BookmarksWindow, EverySelectionKeepsAbsoluteIndexAcrossWindowBoundaries) {
  for (int count : {1, 23, 24, 25, 256}) {
    for (int height : {240, 800}) {
      Harness h(count, height);
      for (int row = 0; row < count; ++row) {
        SCOPED_TRACE(::testing::Message() << "count=" << count << " height=" << height << " row=" << row);
        h.activity.nav.requestSelection(row);
        h.draw();
        ASSERT_EQ(h.target.highlightedRow, row);
        ASSERT_EQ(h.activity.nav.selected.load(), row);
        const auto event = h.tapRow(row);
        ASSERT_EQ(event.value, row);
        ASSERT_EQ(event.action, UiListActivity::ACTION_ROW);
      }
    }
  }
}

TEST(BookmarksWindow, EveryWrappedMeasurementFollowsFullLabelAndEllipsisPrewarm) {
  Harness h;
  int measuredFragments = 0;
  int measuredEllipses = 0;
  h.target.beforeMeasure = [&](const char* text) {
    ++measuredFragments;
    const auto& fullLabel = h.renderer.currentLabel;
    ASSERT_FALSE(fullLabel.empty());
    ASSERT_TRUE(h.renderer.currentLabelIncludesEllipsis);
    ASSERT_FALSE(h.renderer.currentLabelAccumulates);
    int number = 0;
    const auto parsed = std::from_chars(fullLabel.data(), fullLabel.data() + fullLabel.size(), number);
    ASSERT_EQ(parsed.ec, std::errc{});
    ASSERT_GT(number, 0);
    ASSERT_LE(number, h.activity.listCount());
    // A row prewarm must receive the complete snippet, not its first prefix.
    EXPECT_EQ(fullLabel, std::to_string(number) + ". Highlight: " + h.activity.bookmarks[number - 1].snippet);
    std::string fragment = text;
    const std::string ellipsis = "\xE2\x80\xA6";
    if (fragment.ends_with(ellipsis)) {
      ++measuredEllipses;
      fragment.resize(fragment.size() - ellipsis.size());
    }
    // The matching row's complete prewarm must precede every actual SDK
    // wrapping/truncation probe, including probes after a repaint or wrap.
    EXPECT_NE(fullLabel.find(fragment), std::string::npos) << "Measured fragment: " << text;
  };
  h.draw();
  h.activity.nav.requestSelection(255);
  h.draw();
  h.draw();
  EXPECT_GT(measuredFragments, 100);
  EXPECT_GT(measuredEllipses, 0);
}

TEST(BookmarksWindow, PartialTrailingRowHasNoActionUntilFullyVisible) {
  Harness h(256, 240);
  h.draw();
  std::set<int> interactiveRows;
  for (size_t i = 0; i < h.interactions.publishedCount(); ++i)
    interactiveRows.insert(h.interactions.publishedData()[i].value);
  std::vector<int> partialRows;
  std::set_difference(h.target.drawnRows.begin(), h.target.drawnRows.end(), interactiveRows.begin(),
                      interactiveRows.end(), std::back_inserter(partialRows));
  ASSERT_EQ(partialRows.size(), 1u);
  const int partial = partialRows.front();
  EXPECT_EQ(partial, h.activity.nav.top + h.activity.nav.drawnRows);
  h.activity.nav.requestSelection(partial);
  h.draw();
  EXPECT_EQ(h.target.highlightedRow, partial);
  EXPECT_EQ(h.tapRow(partial).value, partial);
}

TEST(BookmarksWindow, ConfirmedDeletionInvalidatesRowsEvenWhenViewportStaysPut) {
  Harness h;
  h.draw();
  h.activity.confirmDeleteBookmark(0);
  ASSERT_TRUE(h.activity.confirmation);
  h.activity.confirmation(ActivityResult{});
  EXPECT_TRUE(h.activity.routingClosed);
  EXPECT_EQ(h.activity.listCount(), 255);
  EXPECT_EQ(h.activity.rowWindowStart, -1);
  const int beforeRepaint = formattedLabels;
  h.draw();
  EXPECT_EQ(formattedLabels - beforeRepaint, 24);
  EXPECT_EQ(h.renderer.batches.size(), 2u);
  EXPECT_EQ(h.activity.rowWindowStart, 0);
  EXPECT_EQ(h.activity.rowItems[0].actionValue, 0);
  EXPECT_EQ(h.activity.bookmarks[0].pageNumber, 1);
  h.activity.activateIndex(h.tapRow(0).value);
  EXPECT_EQ(h.activity.bookmarkResult.pageNumber, 1);
  EXPECT_EQ(h.activity.bookmarkResult.visibleTextOffset, 109u);
}

TEST(BookmarksWindow, DeletingLastRowClampsSelectionAndRebuildsTail) {
  Harness h;
  h.activity.nav.requestSelection(255);
  h.draw();
  h.activity.confirmDeleteBookmark(255);
  h.activity.confirmation(ActivityResult{});
  EXPECT_EQ(h.activity.nav.selected.load(), 254);
  EXPECT_EQ(h.activity.rowWindowStart, -1);
  h.draw();
  EXPECT_EQ(h.target.highlightedRow, 254);
  EXPECT_EQ(h.tapRow(254).value, 254);
  EXPECT_EQ(h.activity.rowWindowCount, 255 - h.activity.rowWindowStart);
}

TEST(BookmarksWindow, CancelledDeletionRetainsCacheAndEmptyListDoesNotPrewarm) {
  Harness h(1);
  h.draw();
  h.activity.confirmDeleteBookmark(0);
  h.activity.confirmation(ActivityResult{true});
  const int afterDialog = formattedLabels;
  h.draw();
  EXPECT_EQ(formattedLabels, afterDialog);
  EXPECT_EQ(h.renderer.batches.size(), 1u);
  EXPECT_EQ(h.activity.listCount(), 1);
  EXPECT_FALSE(h.activity.routingClosed);
  h.activity.confirmation(ActivityResult{});
  EXPECT_TRUE(h.activity.finished);
  EXPECT_TRUE(h.activity.activityResult.isCancelled);
  h.activity.onExit();
  EXPECT_EQ(h.activity.rowWindowStart, -1);
  EXPECT_EQ(h.activity.rowWindowCount, 0);
  for (int i = 0; i < BookmarksActivity::ROW_WINDOW; ++i) {
    EXPECT_TRUE(h.activity.rowLabels[i].empty());
    EXPECT_EQ(h.activity.rowItems[i].label, nullptr);
  }
  Harness empty(0);
  empty.draw();
  EXPECT_EQ(formattedLabels, 0);
  EXPECT_TRUE(empty.renderer.batches.empty());
  EXPECT_EQ(empty.interactions.publishedCount(), 0u);
}
