#include <gtest/gtest.h>

#include <array>

#include "PopupViewport.h"

TEST(PopupViewport, AllSleepOptionsRemainReachableInPortraitAndLandscape) {
  std::array<PopupRowBounds, 13> rows{};
  for (int i = 0; i < 13; ++i) rows[i] = {56 + i * 60, 48};
  for (int height : {640, 300}) {
    PopupViewport view;
    view.configure(rows.back().bottom(), height, 28);
    int selected = 0;
    for (int i = 1; i < 13; ++i) {
      selected = view.navigate(1, selected, rows.data(), static_cast<int>(rows.size()));
      EXPECT_EQ(selected, i);
      EXPECT_GE(rows[i].top, view.offset);
      EXPECT_LE(rows[i].bottom(), view.offset + view.height);
    }
    EXPECT_GT(view.offset, 0);
    EXPECT_EQ(view.navigate(1, selected, rows.data(), static_cast<int>(rows.size())), 0);
    EXPECT_EQ(view.offset, 0);
  }
}

TEST(PopupViewport, MoreThanSixteenOptionsAndReverseWrap) {
  std::array<PopupRowBounds, 50> rows{};
  for (int i = 0; i < 50; ++i) rows[i] = {i * 48, 48};
  PopupViewport view;
  view.configure(2400, 300, 24);
  EXPECT_EQ(view.navigate(-1, 0, rows.data(), static_cast<int>(rows.size())), 49);
  EXPECT_EQ(view.offset, view.maximum());
  view.reveal(rows[35]);
  EXPECT_GE(rows[35].top, view.offset);
  EXPECT_LE(rows[35].bottom(), view.offset + view.height);
}

TEST(PopupViewport, LongMessageCanBeReadInBothDirectionsBeforeChoosing) {
  const PopupRowBounds rows[] = {{1600, 48}, {1660, 48}};
  PopupViewport view;
  view.configure(1708, 300, 28);
  int selected = 0;
  int previous = 0;
  for (int i = 0; i < 10 && rows[0].bottom() > view.offset + view.height; ++i) {
    selected = view.navigate(1, selected, rows, 2);
    EXPECT_EQ(selected, 0);
    EXPECT_LE(view.offset - previous, view.pageStep());
    previous = view.offset;
  }
  EXPECT_TRUE(view.visible(rows[0]));
  EXPECT_EQ(view.navigate(1, selected, rows, 2), 1);
  view.reveal(rows[0]);
  while (view.offset > 0) EXPECT_EQ(view.navigate(-1, 0, rows, 2), 0);
  EXPECT_EQ(view.offset, 0);
}

TEST(PopupViewport, OversizedOptionScrollsBeforeSelectionMoves) {
  const PopupRowBounds rows[] = {{0, 900}, {920, 48}};
  PopupViewport view;
  view.configure(968, 300, 28);
  EXPECT_EQ(view.navigate(1, 0, rows, 2), 0);
  EXPECT_EQ(view.offset, 272);
  EXPECT_EQ(view.navigate(1, 0, rows, 2), 0);
  EXPECT_EQ(view.offset, 544);
  EXPECT_EQ(view.navigate(1, 0, rows, 2), 0);
  EXPECT_EQ(view.offset, 600);
  EXPECT_EQ(view.navigate(1, 0, rows, 2), 1);
}

TEST(PopupViewport, TouchScrollClampsAndResizeRemovesStaleOffset) {
  PopupViewport view;
  view.configure(800, 300, 28);
  view.scroll(10000);
  EXPECT_EQ(view.offset, 500);
  view.scroll(-10000);
  EXPECT_EQ(view.offset, 0);
  view.scroll(500);
  view.configure(800, 900, 28);
  EXPECT_EQ(view.offset, 0);
  EXPECT_EQ(view.maximum(), 0);
}

TEST(PopupViewport, HiddenRowsAreNotVisibleButPartialRowsAre) {
  PopupViewport view;
  view.configure(1000, 300, 28);
  view.scroll(100);
  EXPECT_FALSE(view.visible({0, 100}));
  EXPECT_FALSE(view.visible({400, 50}));
  EXPECT_TRUE(view.visible({75, 50}));
  EXPECT_TRUE(view.visible({375, 50}));
}
