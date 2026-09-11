#include <catch2/catch.hpp>

#include "core/procfs.h"

using nlohmann::json;

TEST_CASE("procfs: /proc/meminfo parses", "[procfs]") {
  auto j = telemetry::procfs::mem_info();
  REQUIRE(j["mem_total_kb"].get<unsigned long long>() > 0);
  REQUIRE(j.contains("mem_used_pct"));
}

TEST_CASE("procfs: cpu ticks and uptime are sane", "[procfs]") {
  auto j = telemetry::procfs::cpu_ticks();
  REQUIRE(j["total"].get<unsigned long long>() > 0);
  REQUIRE(telemetry::procfs::uptime_seconds() > 0.0);
}

TEST_CASE("procfs: netdev totals parse (no loopback)", "[procfs]") {
  auto j = telemetry::procfs::netdev_totals();
  REQUIRE(j.contains("ifaces"));
  for (auto& it : j["ifaces"].items()) {
    REQUIRE(it.key() != "lo");
  }
}

TEST_CASE("procfs: process summary returns a count", "[procfs]") {
  auto j = telemetry::procfs::process_summary();
  REQUIRE(j["process_count"].get<int>() > 0);
}

TEST_CASE("procfs: battery/thermal are best-effort (may be absent on host)", "[procfs]") {
  auto b = telemetry::procfs::battery_info();
  REQUIRE(b.contains("present"));
  auto t = telemetry::procfs::thermal_info();
  (void)t;
}