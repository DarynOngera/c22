#include "core/scheduler.h"

#include <chrono>

namespace telemetry {

Scheduler::~Scheduler() { stop(); }

void Scheduler::add(const std::string& name, int interval_ms, Task task) {
  auto job = std::make_unique<Job>();
  job->name = name;
  job->interval_ms = interval_ms;
  job->fn = std::move(task);
  jobs_.push_back(std::move(job));
}

void Scheduler::start() {
  for (auto& job : jobs_) {
    job->enabled.store(true);
    job->th = std::thread([job = job.get()]() {
      std::unique_lock<std::mutex> lk(job->cv_m);
      while (job->enabled.load()) {
        job->cv.wait_for(lk, std::chrono::milliseconds(job->interval_ms),
                         [job] { return !job->enabled.load(); });
        if (!job->enabled.load()) break;
        lk.unlock();
        job->fn();
        lk.lock();
      }
    });
  }
}

void Scheduler::stop() {
  for (auto& job : jobs_) {
    job->enabled.store(false);
    std::lock_guard<std::mutex> lk(job->cv_m);
    job->cv.notify_all();
  }
  for (auto& job : jobs_) {
    if (job->th.joinable()) job->th.join();
  }
}

}  // namespace telemetry