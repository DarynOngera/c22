#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "telemetry/config.h"
#include "transport/http_client.h"

#include "core/buffer.h"
#include "core/scheduler.h"
#include "core/types.h"

namespace telemetry {

// Singleton telemetry engine. Owns samplers, the in-memory buffer and the
// flushing sink thread. Thread-safe for emit()/start()/stop().
class Engine {
 public:
  static Engine& instance();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void start();  // start with default config + default HTTP client
  void start_with(Config cfg, std::unique_ptr<transport::HttpClient> client);
  void stop();

  bool running() const { return running_.load(); }

  void set_endpoint(const std::string& url);
  void set_device_id(const std::string& id);

  // Queue one message. `type` is one of health|network|app|location|event.
  void emit(const std::string& type, const nlohmann::json& data);

  // Synchronously drain the buffer and POST everything (best effort).
  void flush_all();

  uint64_t messages_sent() const { return sent_.load(); }
  uint64_t messages_dropped() const { return dropped_.load(); }

  // Snapshot of the current configuration.
  Config config() const {
    std::lock_guard<std::mutex> lk(config_mutex_);
    return cfg_;
  }

 private:
  Engine() = default;
  ~Engine();

  void run_sink();
  void start_samplers();
  // Requires start_mutex_ to be held by the caller.
  void flush_all_locked();
  Config snapshot_config() const {
    std::lock_guard<std::mutex> lk(config_mutex_);
    return cfg_;
  }
  nlohmann::json health_payload();
  nlohmann::json network_payload();
  nlohmann::json apps_payload();

  // Lock order: start_mutex_ (outer) -> config_mutex_ (inner). Worker threads
  // only ever take config_mutex_, so stop() can join them while holding
  // start_mutex_ without risking a lock cycle.
  mutable std::mutex start_mutex_;
  mutable std::mutex config_mutex_;
  Config cfg_;
  std::unique_ptr<Buffer> buffer_;
  std::unique_ptr<transport::HttpClient> client_;
  std::unique_ptr<Scheduler> sched_;
  std::thread sink_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};

  std::mutex sampler_state_mutex_;
  uint64_t cpu_prev_total_ = 0;
  uint64_t cpu_prev_idle_ = 0;
  bool have_net_ = false;
  nlohmann::json net_prev_;
  int64_t net_prev_ts_ms_ = 0;

  std::atomic<uint64_t> sent_{0};
  std::atomic<uint64_t> dropped_{0};
};

}  // namespace telemetry