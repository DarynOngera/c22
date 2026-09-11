#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace telemetry {

// Emit a custom application event into the telemetry stream.
//   telemetry::emit("app_launch", {{"screen", "home"}, {"duration_ms", 120}});
void emit(const std::string& name, const nlohmann::json& payload);

}  // namespace telemetry