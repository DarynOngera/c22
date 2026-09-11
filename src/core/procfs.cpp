#include "core/procfs.h"

#include <dirent.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace telemetry::procfs {
namespace {

std::string read_file(const char* path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string trim(std::string s) {
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
          s.end());
  return s;
}

double pct_max_min(double used, double total) {
  return total > 0 ? (used * 100.0 / total) : 0.0;
}

}  // namespace

double uptime_seconds() {
  double sec = 0.0;
  std::ifstream in("/proc/uptime");
  in >> sec;
  return sec;
}

nlohmann::json cpu_ticks() {
  nlohmann::json j = {{"total", 0u}, {"idle", 0u}};
  std::ifstream in("/proc/stat");
  std::string line;
  if (!std::getline(in, line)) return j;
  std::istringstream iss(line);
  std::string cpu;
  unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
  if (!(iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) return j;
  if (cpu != "cpu") return j;
  unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
  j["total"] = total;
  j["idle"] = idle + iowait;
  return j;
}

nlohmann::json mem_info() {
  nlohmann::json j;
  unsigned long long total = 0, avail = 0;
  std::string content = read_file("/proc/meminfo");
  std::istringstream iss(content);
  std::string key, unit;
  unsigned long long value = 0;
  while (iss >> key >> value >> unit) {
    if (key == "MemTotal:") total = value;
    if (key == "MemAvailable:") avail = value;
  }
  j["mem_total_kb"] = total;
  j["mem_available_kb"] = avail;
  j["mem_used_pct"] = pct_max_min(total - avail, total);
  return j;
}

nlohmann::json battery_info() {
  nlohmann::json j;
  j["present"] = false;
  const char* base = "/sys/class/power_supply/battery";
  auto value = [](const char* path) -> long long {
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    long long v = -1;
    if (std::fscanf(f, "%lld", &v) != 1) v = -1;
    std::fclose(f);
    return v;
  };
  long long cap = value("/sys/class/power_supply/battery/capacity");
  long long volt = value("/sys/class/power_supply/battery/voltage_now");
  long long curr = value("/sys/class/power_supply/battery/current_now");
  long long temp_mc = value("/sys/class/power_supply/battery/temp");
  if (cap < 0 && volt < 0 && curr < 0 && temp_mc < 0) return j;
  j["present"] = true;
  if (cap >= 0) j["capacity_pct"] = cap;
  if (volt >= 0) j["voltage_v"] = volt / 1e6;
  if (curr >= 0) j["current_a"] = curr / 1e6;
  if (temp_mc >= 0) j["temperature_c"] = temp_mc / 10.0;
  std::string status = trim(read_file("/sys/class/power_supply/battery/status"));
  if (!status.empty()) j["status"] = status;
  (void)base;
  return j;
}

nlohmann::json thermal_info() {
  nlohmann::json j;
  double max_c = 0;
  DIR* dir = opendir("/sys/devices/virtual/thermal");
  if (!dir) return j;
  dirent* e;
  while ((e = readdir(dir)) != nullptr) {
    if (std::strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
    std::string path = std::string("/sys/devices/virtual/thermal/") + e->d_name + "/temp";
    long long v = -1;
    FILE* f = std::fopen(path.c_str(), "r");
    if (f) {
      if (std::fscanf(f, "%lld", &v) != 1) v = -1;
      std::fclose(f);
    }
    if (v >= 0) max_c = std::max(max_c, v / 1000.0);
  }
  closedir(dir);
  if (max_c > 0) j["max_temp_c"] = max_c;
  return j;
}

nlohmann::json disk_info() {
  nlohmann::json j;
  struct statvfs sv;
  if (statvfs("/data", &sv) != 0) return j;
  unsigned long long total = sv.f_blocks * sv.f_frsize;
  unsigned long long free = sv.f_bavail * sv.f_frsize;
  j["disk_total_kb"] = total / 1024;
  j["disk_free_kb"] = free / 1024;
  j["disk_used_pct"] = pct_max_min(total - free, total);
  return j;
}

nlohmann::json netdev_totals() {
  nlohmann::json j;
  nlohmann::json ifaces = nlohmann::json::object();
  std::string content = read_file("/proc/net/dev");
  std::istringstream iss(content);
  std::string line;
  // skip header lines
  std::getline(iss, line);
  std::getline(iss, line);
  while (std::getline(iss, line)) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const char* skip[] = {"lo", "dummy", "sit", "tun", "tap", "bond"};
    std::string name = trim(line.substr(0, colon));
    bool skip_iface = false;
    for (const char* s : skip) {
      if (name.rfind(s, 0) == 0) {
        skip_iface = true;
        break;
      }
    }
    if (!skip_iface && name.rfind("br", 0) != 0) {
      unsigned long long rx_b, rx_p, tx_b, tx_p;
      std::istringstream ls(line.substr(colon + 1));
      if (ls >> rx_b >> rx_p) {
        // rx: bytes packets errs drop fifo frame compressed multicast
        unsigned long long drop1, drop2;
        ls >> drop1 >> drop2;
        for (int i = 0; i < 4; ++i) ls >> drop1;
        if (ls >> tx_b >> tx_p) {
          ifaces[name] = {{"rx_bytes", rx_b}, {"tx_bytes", tx_b},
                          {"rx_packets", rx_p}, {"tx_packets", tx_p}};
        }
      }
    }
  }
  j["ifaces"] = ifaces;
  return j;
}

nlohmann::json process_summary() {
  nlohmann::json j;
  j["process_count"] = 0;
  nlohmann::json top = nlohmann::json::array();
  DIR* dir = opendir("/proc");
  if (!dir) return j;
  int count = 0;
  std::vector<nlohmann::json> rss;
  dirent* e;
  while ((e = readdir(dir)) != nullptr) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    ++count;
    std::string base = "/proc/";
    base += e->d_name;
    unsigned long long rss_pages = 0;
    FILE* f = std::fopen((base + "/statm").c_str(), "r");
    if (f) {
      unsigned long long size, resident;
      if (std::fscanf(f, "%llu %llu", &size, &resident) == 2) rss_pages = resident;
      std::fclose(f);
    }
    std::string name;
    std::ifstream cmd(base + "/comm");
    std::getline(cmd, name);
    rss.push_back({{"pid", std::stoi(e->d_name)},
                   {"name", trim(name)},
                   {"rss_kb", rss_pages * 4}});
  }
  closedir(dir);
  std::sort(rss.begin(), rss.end(),
            [](const nlohmann::json& a, const nlohmann::json& b) {
              return a["rss_kb"].get<unsigned long long>() > b["rss_kb"].get<unsigned long long>();
            });
  for (size_t i = 0; i < rss.size() && i < 8; ++i) top.push_back(rss[i]);
  j["process_count"] = count;
  j["top_rss"] = top;
  return j;
}

}  // namespace telemetry::procfs