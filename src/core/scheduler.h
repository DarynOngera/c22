#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace telemetry {

// Runs periodic tasks, each on its own thread with a timed wait.
class Scheduler {
 public:
  using Task = std::function<void()>;

  ~Scheduler();

  void add(const std::string& name, int interval_ms, Task task);
  void start();
  void stop();

 private:
  struct Job {
    std::string name;
    int interval_ms;
    Task fn;
    std::thread th;
    std::mutex cv_m;
    std::condition_variable cv;
    std::atomic<bool> enabled{false};
  };

  std::vector<std::unique_ptr<Job>> jobs_;
};

}  // namespace telemetry