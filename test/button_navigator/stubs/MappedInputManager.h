#pragma once

#include <array>
#include <cstdint>
#include <vector>

inline uint32_t navigatorTestTime = 0;
inline uint32_t millis() { return navigatorTestTime; }

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };
  std::array<bool, 9> pressed{};
  std::array<bool, 9> released{};
  std::array<bool, 9> held{};
  unsigned long heldTime = 0;
  mutable std::vector<Button> releaseQueries;

  bool wasPressed(Button button) const { return pressed[static_cast<unsigned>(button)]; }
  bool wasReleased(Button button) const {
    releaseQueries.push_back(button);
    return released[static_cast<unsigned>(button)];
  }
  bool isPressed(Button button) const { return held[static_cast<unsigned>(button)]; }
  unsigned long getHeldTime() const { return heldTime; }
};
