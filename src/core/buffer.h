#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "core/types.h"

namespace telemetry {

// Thread-safe in-memory telemetry buffer.
// Bounded by max_entries (drop-oldest) and TTL (drop-expired).
class Buffer {
 public:
  Buffer(size_t max_entries, int64_t ttl_ms);

  void push(Message m);
  void push_front(std::vector<Message> ms);  // re-queue a failed batch, oldest-first
  bool empty() const;
  size_t size() const;

  // Removes and returns up to `max` oldest non-expired entries.
  std::vector<Message> drain(size_t max, int64_t now_ms);

  // Block until data is present or `stop_pred` turns true, bounded by `timeout`.
  // Returns true if data is available. The predicate keeps the wait both
  // resistant to lost wakeups (a push that arrives before the wait is observed
  // atomically) and interruptible by the caller (stop_pred flips -> returns).
  bool wait_not_empty(std::chrono::milliseconds timeout,
                      const std::function<bool()>& stop_pred = {});

  void wake();  // unblock waiters

 private:
  size_t max_;
  int64_t ttl_;
  std::deque<Message> q_;
  mutable std::mutex m_;
  std::condition_variable cv_;
};

}  // namespace telemetry