#include <catch2/catch.hpp>

#include <thread>

#include "core/buffer.h"
#include "core/types.h"

using namespace telemetry;

TEST_CASE("buffer: push/drain keeps FIFO order", "[buffer]") {
  Buffer b(16, 60'000);
  for (int i = 0; i < 5; ++i) {
    Message m{{}, "event", now_ms(), {{"i", i}}};
    b.push(std::move(m));
  }
  REQUIRE(b.size() == 5);
  auto out = b.drain(16, now_ms());
  REQUIRE(out.size() == 5);
  for (int i = 0; i < 5; ++i) {
    REQUIRE(out[i].data["i"] == i);
  }
  REQUIRE(b.empty());
}

TEST_CASE("buffer: drops oldest at capacity", "[buffer]") {
  Buffer b(3, 60'000);
  for (int i = 0; i < 5; ++i) {
    Message m{{}, "event", now_ms(), {{"i", i}}};
    b.push(std::move(m));
  }
  REQUIRE(b.size() == 3);
  auto out = b.drain(16, now_ms());
  REQUIRE(out.size() == 3);
  REQUIRE(out[0].data["i"] == 2);
  REQUIRE(out[2].data["i"] == 4);
}

TEST_CASE("buffer: expired entries are skipped by TTL", "[buffer]") {
  Buffer b(16, 1'000);
  for (int i = 0; i < 3; ++i) {
    Message m{{}, "event", now_ms() - 5'000, {{"i", i}}};  // older than ttl
    b.push(std::move(m));
  }
  REQUIRE(b.size() == 3);
  auto out = b.drain(16, now_ms());  // now later than ts+ttl -> expired
  REQUIRE(out.empty());
  REQUIRE(b.empty());
}

TEST_CASE("buffer: wait_not_empty wakes on push", "[buffer]") {
  Buffer b(16, 60'000);
  b.wait_not_empty(std::chrono::milliseconds(10));
  REQUIRE(b.empty());  // no data arrived during the wait
  Message m{{}, "event", now_ms(), {{"i", 1}}};
  b.push(std::move(m));
  b.wait_not_empty(std::chrono::milliseconds(100));
  REQUIRE_FALSE(b.empty());  // woken by the push
}

TEST_CASE("buffer: push_front wakes a waiting thread", "[buffer]") {
  Buffer b(16, 60'000);
  std::thread t([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::vector<Message> ms;
    ms.push_back({{}, "event", now_ms(), {}});
    b.push_front(std::move(ms));
  });
  bool woken = b.wait_not_empty(std::chrono::milliseconds(500));
  t.join();
  REQUIRE(woken);
  REQUIRE_FALSE(b.empty());
}

TEST_CASE("buffer: push_front preserves batch FIFO order", "[buffer]") {
  Buffer b(16, 60'000);
  std::vector<Message> ms;
  for (int i = 0; i < 3; ++i) {
    ms.push_back({{}, "event", now_ms(), {{"i", i}}});
  }
  b.push_front(std::move(ms));
  auto out = b.drain(16, now_ms());
  REQUIRE(out.size() == 3);
  REQUIRE(out[0].data["i"] == 0);
  REQUIRE(out[2].data["i"] == 2);
}

TEST_CASE("buffer: push_front entries respect TTL", "[buffer]") {
  Buffer b(16, 1'000);
  std::vector<Message> ms;
  for (int i = 0; i < 3; ++i) {
    ms.push_back({{}, "event", now_ms() - 5'000, {{"i", i}}});  // expired
  }
  b.push_front(std::move(ms));
  REQUIRE(b.size() == 3);
  auto out = b.drain(16, now_ms());
  REQUIRE(out.empty());
}
