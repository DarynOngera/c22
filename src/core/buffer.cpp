#include "core/buffer.h"

namespace telemetry {

Buffer::Buffer(size_t max_entries, int64_t ttl_ms) : max_(max_entries), ttl_(ttl_ms) {}

void Buffer::push(Message m) {
  {
    std::lock_guard<std::mutex> lk(m_);
    if (max_ > 0 && q_.size() >= max_) q_.pop_front();  // drop oldest at capacity
    q_.push_back(std::move(m));
  }
  cv_.notify_one();
}

void Buffer::push_front(std::vector<Message> ms) {
  if (ms.empty()) return;
  std::lock_guard<std::mutex> lk(m_);
  for (auto it = ms.rbegin(); it != ms.rend(); ++it) {
    if (max_ > 0 && q_.size() >= max_) q_.pop_back();  // drop newest at capacity
    q_.push_front(std::move(*it));
  }
  cv_.notify_all();
}

bool Buffer::empty() const {
  std::lock_guard<std::mutex> lk(m_);
  return q_.empty();
}

size_t Buffer::size() const {
  std::lock_guard<std::mutex> lk(m_);
  return q_.size();
}

std::vector<Message> Buffer::drain(size_t max, int64_t now_ms) {
  std::vector<Message> out;
  std::lock_guard<std::mutex> lk(m_);
  while (!q_.empty() && out.size() < max) {
    Message m = std::move(q_.front());
    q_.pop_front();
    if (ttl_ > 0 && now_ms - m.ts_ms > ttl_) continue;  // expired, skip
    out.push_back(std::move(m));
  }
  return out;
}

bool Buffer::wait_not_empty(std::chrono::milliseconds timeout,
                            const std::function<bool()>& stop_pred) {
  std::unique_lock<std::mutex> lk(m_);
  auto ready = [&] {
    if (stop_pred && stop_pred()) return true;
    return !q_.empty();
  };
  // Predicate waits check the condition atomically with sleeping, so a push
  // that arrives just before the wait is still observed (no lost wakeup).
  // stop_pred makes the wait interruptible on shutdown.
  if (ready()) return !q_.empty();
  cv_.wait_for(lk, timeout, ready);
  return !q_.empty();
}

void Buffer::wake() { cv_.notify_all(); }

}  // namespace telemetry