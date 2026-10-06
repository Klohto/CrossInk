#pragma once

#include <cstdint>

// Prepare one cover between input checks. Combine quick completions into one
// screen refresh, while a slow completion can update the screen immediately.
class CoverRefreshBatch {
  uint32_t startedAt_ = 0;
  uint8_t changed_ = 0;

 public:
  void reset(uint32_t now) {
    startedAt_ = now;
    changed_ = 0;
  }
  void changed() { ++changed_; }
  bool take(uint32_t now, bool complete) {
    if (!changed_ || (!complete && changed_ < 2 && now - startedAt_ < 250)) return false;
    reset(now);
    return true;
  }
};
