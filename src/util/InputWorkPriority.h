#pragma once

#include <atomic>
#include <cstdint>

// Shared policy for optional work. Visible UI and explicit user requests run
// immediately; preparation waits until input has been quiet for 250 ms.
namespace InputWorkPriority {
inline std::atomic<uint32_t> epoch{0};
inline std::atomic<uint32_t> lastInputMs{0};
inline std::atomic<bool> sawInput{false};
constexpr uint32_t QUIET_MS = 250;
inline void notify(const uint32_t now) {
  lastInputMs.store(now, std::memory_order_relaxed);
  sawInput.store(true, std::memory_order_relaxed);
  epoch.fetch_add(1, std::memory_order_relaxed);
}
inline bool canPrepare(const uint32_t now) {
  return !sawInput.load(std::memory_order_relaxed) || now - lastInputMs.load(std::memory_order_relaxed) >= QUIET_MS;
}
struct Ticket {
  uint32_t value = epoch.load(std::memory_order_relaxed);
  bool interrupted() const { return value != epoch.load(std::memory_order_relaxed); }
};
}  // namespace InputWorkPriority
