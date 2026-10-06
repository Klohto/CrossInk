#include <gtest/gtest.h>

#include <functional>
#include <limits>

#include "ButtonNavigator.h"

using Button = MappedInputManager::Button;

class ButtonNavigatorTest : public ::testing::Test {
 protected:
  MappedInputManager input;
  ButtonNavigator navigator{50, 100};
  int calls = 0;

  void SetUp() override {
    navigatorTestTime = 200;
    ButtonNavigator::setMappedInputManager(input);
  }

  void set(std::array<bool, 9>& values, Button button, bool value = true) {
    values[static_cast<unsigned>(button)] = value;
  }

  void repeat() {
    set(input.held, Button::Down);
    input.heldTime = 101;
    navigator.onNextContinuous([&] { ++calls; });
  }
};

TEST_F(ButtonNavigatorTest, IdlePollDoesNotCallCallback) {
  navigator.onNext([&] { ++calls; });
  navigator.onPrevious([&] { ++calls; });
  navigator.onNextRelease([&] { ++calls; });
  EXPECT_EQ(calls, 0);
}

TEST_F(ButtonNavigatorTest, ShortReleaseCallsOnceAndStopsAtFirstMatchingButton) {
  set(input.released, Button::Down);
  set(input.released, Button::Right);
  navigator.onNextRelease([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  ASSERT_EQ(input.releaseQueries.size(), 1u);
  EXPECT_EQ(input.releaseQueries.front(), Button::Down);
}

TEST_F(ButtonNavigatorTest, ReleaseAfterRepeatResetsStateWithoutExtraMove) {
  repeat();
  EXPECT_EQ(calls, 1);
  set(input.released, Button::Down);
  navigator.onNextRelease([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  navigator.onNextRelease([&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, RepeatRequiresBothStrictTimeThresholds) {
  set(input.held, Button::Down);
  navigatorTestTime = 50;
  input.heldTime = 101;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 0);
  navigatorTestTime = 51;
  input.heldTime = 100;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 0);
  input.heldTime = 101;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  navigatorTestTime = 101;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  navigatorTestTime = 102;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, RepeatUsesTimeAfterCallback) {
  set(input.held, Button::Down);
  input.heldTime = 101;
  navigator.onNextContinuous([&] {
    ++calls;
    navigatorTestTime = 1000;
  });
  navigatorTestTime = 1050;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  navigatorTestTime = 1051;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, PressCallbackCanChangeInputForRepeatInSamePoll) {
  MappedInputManager replacement;
  set(replacement.held, Button::Down);
  replacement.heldTime = 101;
  set(input.pressed, Button::Down);
  navigator.onNext([&] {
    ++calls;
    ButtonNavigator::setMappedInputManager(replacement);
  });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, ReleaseResetsRepeatStateAfterCallbackReturns) {
  set(input.released, Button::Down);
  navigator.onNextRelease([&] { repeat(); });
  EXPECT_EQ(calls, 1);
  navigator.onNextRelease([&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, RepeatIntervalSurvivesClockWrap) {
  navigatorTestTime = std::numeric_limits<uint32_t>::max() - 4;
  repeat();
  navigatorTestTime = 45;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  navigatorTestTime = 46;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, BothHeldDirectionsProduceOneRepeat) {
  set(input.held, Button::Right);
  repeat();
  EXPECT_EQ(calls, 1);
}

TEST_F(ButtonNavigatorTest, ExistingFunctionCallbacksAndMutableLambdasWork) {
  set(input.pressed, Button::Up);
  const std::function<void()> callback = [&] { ++calls; };
  navigator.onPreviousPress(callback);
  navigator.onPreviousPress([count = 0, &calls = calls]() mutable { calls += ++count; });
  EXPECT_EQ(calls, 2);
}
