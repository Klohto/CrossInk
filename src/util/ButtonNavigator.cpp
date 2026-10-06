#include "ButtonNavigator.h"

#include <algorithm>

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

bool ButtonNavigator::hasPress(const Buttons& buttons) const {
  return std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });
}

bool ButtonNavigator::hasRelease(const Buttons& buttons) const {
  return std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });
}

bool ButtonNavigator::canNavigateContinuously(const Buttons& buttons) const {
  return std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously();
  });
}

bool ButtonNavigator::shouldNavigateContinuously() const {
  if (!mappedInput) return false;

  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + 1) % totalItems;
}

int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - 1) % totalItems;
}

int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return nextIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) {
    return (currentPageIndex + 1) * itemsPerPage;
  }

  return 0;
}

int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return previousIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) {
    return (currentPageIndex - 1) * itemsPerPage;
  }

  return lastPageIndex * itemsPerPage;
}
