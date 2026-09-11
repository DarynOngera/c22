#include <catch2/catch.hpp>

#include "core/types.h"

using namespace telemetry;

TEST_CASE("message serializes into the expected JSON shape", "[json]") {
  Message m;
  m.device_id = "dev-1";
  m.type = "health";
  m.ts_ms = 1234;
  m.data = {{"cpu_used_pct", 12.5}, {"battery", {{"present", true}}}};

  auto j = m.to_json();
  REQUIRE(j["ts"] == 1234);
  REQUIRE(j["device_id"] == "dev-1");
  REQUIRE(j["type"] == "health");
  REQUIRE(j["data"]["cpu_used_pct"] == Approx(12.5));
  REQUIRE(j["data"]["battery"]["present"] == true);
}

TEST_CASE("batch payload is a JSON array with one object per message", "[json]") {
  std::vector<Message> msgs;
  for (int i = 0; i < 3; ++i) {
    Message m{{}, "event", 100 + i, {{"seq", i}}};
    msgs.push_back(std::move(m));
  }
  auto body = build_batch_payload(msgs);
  using json = nlohmann::json;
  auto arr = json::parse(body);
  REQUIRE(arr.is_array());
  REQUIRE(arr.size() == 3);
  REQUIRE(arr[2]["type"] == "event");
  REQUIRE(arr[2]["data"]["seq"] == 2);
}