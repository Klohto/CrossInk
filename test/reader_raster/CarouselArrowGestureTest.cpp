#include <gtest/gtest.h>

#include "src/util/CarouselArrowGesture.h"

namespace {
using Action = CarouselArrowGesture::Action;
TEST(CarouselArrowGesture, ShortTapMovesOnceAfterRelease) {
  CarouselArrowGesture gesture;
  EXPECT_EQ(gesture.update(false, true, false, true, false, false, 100), Action::None);
  EXPECT_EQ(gesture.update(false, false, false, false, false, true, 180), Action::Right);
  EXPECT_EQ(gesture.update(false, false, false, false, false, true, 200), Action::None);
}
TEST(CarouselArrowGesture, HoldChangesRowsOnceAndKeepsTheBookSelection) {
  for (bool left : {false, true}) {
    CarouselArrowGesture gesture;
    EXPECT_EQ(gesture.update(left, !left, left, !left, false, false, 100), Action::None);
    EXPECT_EQ(gesture.update(false, false, left, !left, false, false, 699), Action::None);
    EXPECT_EQ(gesture.update(false, false, left, !left, false, false, 700), Action::ToggleRows);
    EXPECT_EQ(gesture.update(false, false, left, !left, false, false, 1100), Action::None);
    EXPECT_EQ(gesture.update(false, false, false, false, left, !left, 1200), Action::None);
  }
}
TEST(CarouselArrowGesture, HandlesDelayedReleaseOpposingButtonsAndClockWrap) {
  CarouselArrowGesture gesture;
  gesture.update(true, false, true, false, false, false, 100);
  EXPECT_EQ(gesture.update(false, false, false, false, true, false, 1100), Action::ToggleRows);
  gesture.update(true, false, true, false, false, false, 2000);
  EXPECT_EQ(gesture.update(false, true, true, true, false, false, 2100), Action::None);
  EXPECT_EQ(gesture.update(false, false, false, false, true, true, 2200), Action::None);
  gesture.update(true, false, true, false, false, false, UINT32_MAX - 100);
  EXPECT_EQ(gesture.update(false, false, true, false, false, false, 500), Action::ToggleRows);
}
}  // namespace
