#pragma once

#include <nlohmann/json.hpp>

namespace telemetry::procfs {

// /proc/uptime -> seconds. Returns 0 if unreadable.
double uptime_seconds();

// /proc/stat CPU ticks -> {total, idle}. Returns zeros if unreadable.
nlohmann::json cpu_ticks();

// /proc/meminfo -> {mem_total_kb, mem_available_kb, mem_used_pct}.
nlohmann::json mem_info();

// /sys/class/power_supply -> battery/capacity etc. Absent keys when unavailable.
nlohmann::json battery_info();

// /sys/class/thermal thermal zones -> max temperature in Celsius (best effort).
nlohmann::json thermal_info();

// statvfs("/data") -> {disk_total_kb, disk_free_kb, disk_used_pct}.
nlohmann::json disk_info();

// /proc/net/dev -> {ifaces: {name: {rx_bytes, tx_bytes, rx_packets, tx_packets}}}.
nlohmann::json netdev_totals();

// /proc scan -> {process_count, top_rss: [{pid, name, rss_kb}]}.
nlohmann::json process_summary();

}  // namespace telemetry::procfs