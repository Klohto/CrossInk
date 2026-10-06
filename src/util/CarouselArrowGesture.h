#pragma once

#include <cstdint>

// Short arrows move on release. A hold changes rows once and consumes its
// release, preserving the selected book when the menu opens.
class CarouselArrowGesture {
 public:
  static constexpr uint32_t HOLD_MS = 600;
  enum class Action { None, Left, Right, ToggleRows };
  Action update(bool leftEdge, bool rightEdge, bool leftDown, bool rightDown, bool leftReleased, bool rightReleased,
                uint32_t now) {
    if (leftDown && rightDown) {
      blocked_ = true;
      direction_ = 0;
    }
    if (blocked_) {
      if (!leftDown && !rightDown) blocked_ = false;
      return Action::None;
    }
    if (leftEdge || rightEdge) {
      direction_ = leftEdge ? -1 : 1;
      started_ = now;
      held_ = false;
    }
    if (!direction_) return Action::None;
    const bool down = direction_ < 0 ? leftDown : rightDown;
    const bool released = direction_ < 0 ? leftReleased : rightReleased;
    const bool longPress = now - started_ >= HOLD_MS;
    if (released) {
      const Action result = held_            ? Action::None
                            : longPress      ? Action::ToggleRows
                            : direction_ < 0 ? Action::Left
                                             : Action::Right;
      direction_ = 0;
      return result;
    }
    if (down && longPress && !held_) {
      held_ = true;
      return Action::ToggleRows;
    }
    if (!down) direction_ = 0;
    return Action::None;
  }

 private:
  uint32_t started_ = 0;
  int8_t direction_ = 0;
  bool held_ = false;
  bool blocked_ = false;
};
