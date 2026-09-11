#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry {

struct Config {
  std::string endpoint = "http://10.0.2.2:8080/telemetry";
  std::string device_id;
  bool tls_verify = true;

  int health_interval_ms = 10'000;
  int network_interval_ms = 10'000;
  int apps_interval_ms = 30'000;
  int flush_interval_ms = 15'000;
  int http_timeout_ms = 10'000;

  size_t buffer_max = 4096;
  int64_t buffer_ttl_ms = 24LL * 3600 * 1000;  // drop entries older than 24h
  size_t batch_max = 200;                       // entries per POST

  int retry_backoff_ms = 2'000;   // initial retry delay
  int retry_backoff_max_ms = 30'000;
};

}  // namespace telemetry