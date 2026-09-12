#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>

#include "core/procfs.h"
#include "core/types.h"
#include "telemetry/event.h"

#ifdef __ANDROID__
#include <android/log.h>
#define TLOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "telemetry", fmt, ##__VA_ARGS__)
#else
#define TLOG(fmt, ...) ::fprintf(stderr, "[telemetry] " fmt "\n", ##__VA_ARGS__)
#endif

namespace telemetry {

Engine& Engine::instance() {
  static Engine engine;
  return engine;
}

void Engine::set_endpoint(const std::string& url) {
  std::lock_guard<std::mutex> lk(config_mutex_);
  cfg_.endpoint = url;
}

void Engine::set_device_id(const std::string& id) {
  std::lock_guard<std::mutex> lk(config_mutex_);
  cfg_.device_id = id;
}

void Engine::start() {
  Config cfg;
  {
    std::lock_guard<std::mutex> lk(config_mutex_);
    cfg = cfg_;
  }
  start_with(std::move(cfg), transport::make_default_http_client());
}

void Engine::start_with(Config cfg, std::unique_ptr<transport::HttpClient> client) {
  std::lock_guard<std::mutex> lk(start_mutex_);
  if (running_.load()) return;
  if (!client) client = transport::make_default_http_client();
  {
    std::lock_guard<std::mutex> ck(config_mutex_);
    cfg_ = cfg;
  }
  client_ = std::move(client);
  client_->set_verify(cfg.tls_verify);
  buffer_ = std::make_unique<Buffer>(cfg.buffer_max, cfg.buffer_ttl_ms);
  sched_ = std::make_unique<Scheduler>();
  start_samplers();
  sched_->start();
  stopping_.store(false);
  running_.store(true);
  sent_.store(0);
  dropped_.store(0);
  sink_thread_ = std::thread(&Engine::run_sink, this);
  TLOG("engine started: endpoint=%s device=%s", cfg.endpoint.c_str(),
       cfg.device_id.c_str());
}

Engine::~Engine() { stop(); }

void Engine::stop() {
  std::lock_guard<std::mutex> lk(start_mutex_);
  if (!running_.load()) return;
  stopping_.store(true);
  running_.store(false);
  if (sched_) sched_->stop();
  sched_.reset();
  if (buffer_) buffer_->wake();
  if (sink_thread_.joinable()) sink_thread_.join();
  flush_all_locked();  // best-effort final delivery (already holding the lock)
  client_.reset();
  TLOG("engine stopped: sent=%llu dropped=%llu", (unsigned long long)sent_.load(),
       (unsigned long long)dropped_.load());
}

void Engine::emit(const std::string& type, const nlohmann::json& data) {
  std::lock_guard<std::mutex> lk(config_mutex_);
  if (!running_.load() || !buffer_) return;
  Message m;
  m.type = type;
  m.ts_ms = now_ms();
  m.data = data;
  m.device_id = cfg_.device_id;
  buffer_->push(std::move(m));
}

nlohmann::json Engine::health_payload() {
  nlohmann::json j;
  j["uptime_s"] = procfs::uptime_seconds();
  j["mem"] = procfs::mem_info();
  j["disk"] = procfs::disk_info();
  j["battery"] = procfs::battery_info();
  j["thermal"] = procfs::thermal_info();

  nlohmann::json ticks = procfs::cpu_ticks();
  uint64_t total = ticks.value("total", 0ull);
  uint64_t idle = ticks.value("idle", 0ull);
  double cpu_pct = 0.0;
  {
    std::lock_guard<std::mutex> lk(sampler_state_mutex_);
    // Guard against counter regression (e.g. CPU hotplug), which would
    // otherwise underflow on unsigned subtraction.
    if (cpu_prev_total_ != 0 && total >= cpu_prev_total_ && idle >= cpu_prev_idle_) {
      uint64_t dt = total - cpu_prev_total_;
      uint64_t di = idle - cpu_prev_idle_;
      if (dt > 0) cpu_pct = std::max(0.0, 100.0 * (1.0 - static_cast<double>(di) / dt));
    }
    cpu_prev_total_ = total;
    cpu_prev_idle_ = idle;
  }
  j["cpu_used_pct"] = cpu_pct;
  return j;
}

nlohmann::json Engine::network_payload() {
  nlohmann::json j;
  nlohmann::json totals = procfs::netdev_totals();
  uint64_t rx_sum = 0, tx_sum = 0, rx_p = 0, tx_p = 0;
  for (auto& it : totals["ifaces"].items()) {
    rx_sum += it.value().value("rx_bytes", 0ull);
    tx_sum += it.value().value("tx_bytes", 0ull);
    rx_p += it.value().value("rx_packets", 0ull);
    tx_p += it.value().value("tx_packets", 0ull);
  }
  j["ifaces"] = totals["ifaces"];
  j["total_rx_bytes"] = rx_sum;
  j["total_tx_bytes"] = tx_sum;
  int64_t now = now_ms();
  {
    std::lock_guard<std::mutex> lk(sampler_state_mutex_);
    if (have_net_) {
      uint64_t p_rx = net_prev_.value("total_rx_bytes", 0ull);
      uint64_t p_tx = net_prev_.value("total_tx_bytes", 0ull);
      double dt_s = std::max(1.0, (now - net_prev_ts_ms_) / 1000.0);
      // Interface reset (toggle/reassign) can regress the counters; clamp so
      // unsigned subtraction cannot wrap into a phantom multigigabit rate.
      j["rx_rate_bps"] = rx_sum >= p_rx ? (rx_sum - p_rx) * 8.0 / dt_s : 0.0;
      j["tx_rate_bps"] = tx_sum >= p_tx ? (tx_sum - p_tx) * 8.0 / dt_s : 0.0;
    } else {
      j["rx_rate_bps"] = 0.0;
      j["tx_rate_bps"] = 0.0;
    }
    net_prev_["total_rx_bytes"] = rx_sum;
    net_prev_["total_tx_bytes"] = tx_sum;
    net_prev_ts_ms_ = now;
    have_net_ = true;
  }
  j["rx_packets"] = rx_p;
  j["tx_packets"] = tx_p;
  return j;
}

// Defined in android-specific translation units to keep the core host-testable.
#ifdef __ANDROID__
namespace jni {
nlohmann::json jni_network_active();
nlohmann::json jni_recent_apps();
}
#endif

nlohmann::json Engine::apps_payload() {
  nlohmann::json j = procfs::process_summary();
#ifdef __ANDROID__
  j["recent"] = jni::jni_recent_apps();
#endif
  return j;
}

void Engine::start_samplers() {
  if (!sched_) return;
  sched_->add("health", cfg_.health_interval_ms, [this] {
    emit("health", health_payload());
  });

  if (cfg_.network_interval_ms > 0) {
    sched_->add("network", cfg_.network_interval_ms, [this] {
      nlohmann::json p = network_payload();
#ifdef __ANDROID__
      p["active"] = jni::jni_network_active();
#endif
      emit("network", p);
    });
  }

  if (cfg_.apps_interval_ms > 0) {
    sched_->add("apps", cfg_.apps_interval_ms, [this] { emit("app", apps_payload()); });
  }
}

void Engine::flush_all_locked() {
  if (!buffer_ || !client_) return;
  Config cfg = snapshot_config();
  std::vector<Message> all =
      buffer_->drain(std::numeric_limits<size_t>::max(), now_ms());
  if (all.empty()) return;
  std::string payload = build_batch_payload(all);
  transport::HttpClient::Result res =
      client_->post(cfg.endpoint, payload, cfg.http_timeout_ms);
  if (res.ok) {
    sent_ += all.size();
    TLOG("posted %zu msgs (http %ld)", all.size(), res.http_code);
    return;
  }
  dropped_ += all.size();
  TLOG("post failed: %s", res.error.c_str());
  buffer_->push_front(std::move(all));
}

void Engine::flush_all() {
  std::lock_guard<std::mutex> lk(start_mutex_);
  flush_all_locked();
}

void Engine::run_sink() {
  std::vector<Message> pending;
  int backoff = snapshot_config().retry_backoff_ms;
  while (!stopping_.load()) {
    if (pending.empty()) {
      Config cfg = snapshot_config();
      buffer_->wait_not_empty(std::chrono::milliseconds(cfg.flush_interval_ms),
                              [this] { return stopping_.load(); });
      pending = buffer_->drain(cfg.batch_max, now_ms());
      if (pending.empty()) continue;
    }
    std::string payload = build_batch_payload(pending);
    Config cfg = snapshot_config();
    transport::HttpClient::Result res =
        client_->post(cfg.endpoint, payload, cfg.http_timeout_ms);
    if (res.ok) {
      sent_ += pending.size();
      TLOG("posted %zu msgs (http %ld)", pending.size(), res.http_code);
      pending.clear();
      backoff = cfg.retry_backoff_ms;
    } else {
      TLOG("post failed (%s); retrying in %d ms", res.error.c_str(), backoff);
      for (int i = 0; i < backoff / 10 && !stopping_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      backoff = std::min(backoff * 2, cfg.retry_backoff_max_ms);
    }
  }
}

}  // namespace telemetry

void telemetry::emit(const std::string& name, const nlohmann::json& payload) {
  Engine::instance().emit("event", {{"name", name}, {"payload", payload}});
}