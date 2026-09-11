#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <catch2/catch.hpp>

#include "core/engine.h"

using namespace telemetry;

namespace {

template <typename Body>
struct StopEngineOnExit {
  explicit StopEngineOnExit(Body b) : body(b) {}
  ~StopEngineOnExit() { body(); }
  Body body;
};

struct MockClient final : public transport::HttpClient {
  Result post(const std::string& url, const std::string& body, int timeout_ms) override {
    std::lock_guard<std::mutex> lk(m_);
    last_url_ = url;
    last_body_ = body;
    bodies_.push_back(body);
    ++calls_;
    (void)timeout_ms;
    Result r;
    r.ok = ok_.load();
    r.http_code = r.ok ? 200 : 503;
    if (!r.ok) r.error = "mock failure";
    return r;
  }

  void set_verify(bool) override {}

  int calls() const {
    std::lock_guard<std::mutex> lk(m_);
    return calls_;
  }
  std::vector<std::string> bodies() const {
    std::lock_guard<std::mutex> lk(m_);
    return bodies_;
  }
  std::atomic<bool> ok_{true};

 private:
  mutable std::mutex m_;
  std::string last_url_;
  std::string last_body_;
  std::vector<std::string> bodies_;
  int calls_ = 0;
};

bool wait_until(const std::function<bool()>& pred, int timeout_ms) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return pred();
}

std::vector<std::string> collect_names(const MockClient& mc) {
  std::vector<std::string> names;
  for (const auto& b : mc.bodies()) {
    auto arr = nlohmann::json::parse(b);
    for (const auto& e : arr) names.push_back(e["data"]["name"].get<std::string>());
  }
  return names;
}

}  // namespace

TEST_CASE("engine: queues then delivers everything after transport recovers",
          "[engine]") {
  auto mc = std::make_unique<MockClient>();
  MockClient* raw = mc.get();
  raw->ok_.store(false);

  Config cfg;
  cfg.endpoint = "http://host.test/telemetry";
  cfg.flush_interval_ms = 10;
  cfg.retry_backoff_ms = 20;
  cfg.retry_backoff_max_ms = 40;
  cfg.batch_max = 100;

  auto& engine = Engine::instance();
  StopEngineOnExit guard([&] { engine.stop(); });
  engine.start_with(cfg, std::move(mc));
  engine.emit("event", {{"name", "t1"}, {"n", 1}});
  engine.emit("event", {{"name", "t2"}, {"n", 2}});

  REQUIRE(wait_until([&] { return raw->calls() >= 1; }, 2000));
  REQUIRE(engine.messages_sent() == 0);  // transport failing; nothing lost

  raw->ok_.store(true);
  REQUIRE(wait_until([&] { return engine.messages_sent() == 2; }, 3000));

  auto names = collect_names(*raw);
  REQUIRE(std::find(names.begin(), names.end(), "t1") != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "t2") != names.end());
}

TEST_CASE("engine: flush_all delivers synchronously", "[engine]") {
  auto mc = std::make_unique<MockClient>();
  MockClient* raw = mc.get();

  Config cfg;
  cfg.endpoint = "http://host.test/telemetry";
  cfg.flush_interval_ms = 60'000;  // keep the sink idle for this test
  cfg.batch_max = 1000;
  cfg.device_id = "test-dev";

  auto& engine = Engine::instance();
  StopEngineOnExit guard([&] { engine.stop(); });
  engine.start_with(cfg, std::move(mc));
  engine.emit("location", {{"lat", 1.5}, {"lng", 2.5}});

  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  engine.flush_all();
  REQUIRE(engine.messages_sent() == 1);

  auto bodies = raw->bodies();
  REQUIRE_FALSE(bodies.empty());
  auto body = nlohmann::json::parse(bodies.front());
  REQUIRE(body.is_array());
  REQUIRE(body.size() == 1);
  REQUIRE(body[0]["device_id"] == "test-dev");
  REQUIRE(body[0]["type"] == "location");
}