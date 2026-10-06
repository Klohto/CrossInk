#pragma once

#include <array>

#include "MappedInputManager.h"

class ButtonNavigator final {
  using Buttons = std::array<MappedInputManager::Button, 2>;

  const uint16_t continuousStartMs;
  const uint16_t continuousIntervalMs;
  uint32_t lastContinuousNavTime = 0;
  static const MappedInputManager* mappedInput;

  [[nodiscard]] bool shouldNavigateContinuously() const;
  [[nodiscard]] bool hasPress(const Buttons& buttons) const;
  [[nodiscard]] bool hasRelease(const Buttons& buttons) const;
  [[nodiscard]] bool canNavigateContinuously(const Buttons& buttons) const;

 public:
  explicit ButtonNavigator(const uint16_t continuousIntervalMs = 500, const uint16_t continuousStartMs = 500)
      : continuousStartMs(continuousStartMs), continuousIntervalMs(continuousIntervalMs) {}

  static void setMappedInputManager(const MappedInputManager& mappedInputManager) { mappedInput = &mappedInputManager; }

  // These callbacks run in this poll and are never stored.
  template <typename Callback>
  void onNext(Callback&& callback) {
    onNextPress(callback);
    onNextContinuous(callback);
  }

  template <typename Callback>
  void onPrevious(Callback&& callback) {
    onPreviousPress(callback);
    onPreviousContinuous(callback);
  }

  template <typename Callback>
  void onPressAndContinuous(const Buttons& buttons, Callback&& callback) {
    onPress(buttons, callback);
    onContinuous(buttons, callback);
  }

  template <typename Callback>
  void onNextPress(Callback&& callback) {
    onPress(getNextButtons(), callback);
  }

  template <typename Callback>
  void onPreviousPress(Callback&& callback) {
    onPress(getPreviousButtons(), callback);
  }

  template <typename Callback>
  void onPress(const Buttons& buttons, Callback&& callback) {
    if (hasPress(buttons)) callback();
  }

  template <typename Callback>
  void onNextRelease(Callback&& callback) {
    onRelease(getNextButtons(), callback);
  }

  template <typename Callback>
  void onPreviousRelease(Callback&& callback) {
    onRelease(getPreviousButtons(), callback);
  }

  template <typename Callback>
  void onRelease(const Buttons& buttons, Callback&& callback) {
    if (hasRelease(buttons)) {
      if (lastContinuousNavTime == 0) callback();
      lastContinuousNavTime = 0;
    }
  }

  template <typename Callback>
  void onNextContinuous(Callback&& callback) {
    onContinuous(getNextButtons(), callback);
  }

  template <typename Callback>
  void onPreviousContinuous(Callback&& callback) {
    onContinuous(getPreviousButtons(), callback);
  }

  template <typename Callback>
  void onContinuous(const Buttons& buttons, Callback&& callback) {
    if (canNavigateContinuously(buttons)) {
      callback();
      lastContinuousNavTime = millis();
    }
  }

  [[nodiscard]] static int nextIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousIndex(int currentIndex, int totalItems);

  [[nodiscard]] static int nextPageIndex(int currentIndex, int totalItems, int itemsPerPage);
  [[nodiscard]] static int previousPageIndex(int currentIndex, int totalItems, int itemsPerPage);

  [[nodiscard]] static constexpr Buttons getNextButtons() {
    return {MappedInputManager::Button::Down, MappedInputManager::Button::Right};
  }
  [[nodiscard]] static constexpr Buttons getPreviousButtons() {
    return {MappedInputManager::Button::Up, MappedInputManager::Button::Left};
  }
};
