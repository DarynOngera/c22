#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace telemetry {

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

struct Message {
  std::string device_id;
  std::string type;  // health | network | app | location | event
  int64_t ts_ms;
  nlohmann::json data;

  nlohmann::json to_json() const {
    return {{"ts", ts_ms}, {"device_id", device_id}, {"type", type}, {"data", data}};
  }
};

inline std::string build_batch_payload(const std::vector<Message>& msgs) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& m : msgs) arr.push_back(m.to_json());
  return arr.dump();
}

}  // namespace telemetry