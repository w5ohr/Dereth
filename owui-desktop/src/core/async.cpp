#include "core/async.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace async {
namespace {

std::vector<std::thread> gWorkers;
std::deque<std::function<void()>> gJobs;
std::mutex gJobMutex;
std::condition_variable gJobCv;
std::atomic<bool> gRunning{false};
std::atomic<int> gPending{0};

std::deque<std::function<void()>> gMainQueue;
std::mutex gMainMutex;

void workerLoop() {
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(gJobMutex);
      gJobCv.wait(lock, [] { return !gJobs.empty() || !gRunning.load(); });
      if (!gRunning.load() && gJobs.empty()) return;
      job = std::move(gJobs.front());
      gJobs.pop_front();
    }
    job();
    gPending.fetch_sub(1);
  }
}

}  // namespace

void start(int threads) {
  if (gRunning.exchange(true)) return;
  for (int i = 0; i < threads; ++i) gWorkers.emplace_back(workerLoop);
}

void shutdown() {
  if (!gRunning.exchange(false)) {
    // Already stopped: still make sure nothing is left joinable.
    for (auto& t : gWorkers) {
      if (t.joinable()) t.join();
    }
    gWorkers.clear();
    return;
  }
  gJobCv.notify_all();
  for (auto& t : gWorkers) {
    if (t.joinable()) t.join();
  }
  gWorkers.clear();
}

void run(std::function<void()> job) {
  gPending.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(gJobMutex);
    gJobs.push_back(std::move(job));
  }
  gJobCv.notify_one();
}

void runDetached(std::function<void()> job) {
  gPending.fetch_add(1);
  std::thread([job = std::move(job)] {
    job();
    gPending.fetch_sub(1);
  }).detach();
}

void post(std::function<void()> fn) {
  std::lock_guard<std::mutex> lock(gMainMutex);
  gMainQueue.push_back(std::move(fn));
}

void drain() {
  std::deque<std::function<void()>> batch;
  {
    std::lock_guard<std::mutex> lock(gMainMutex);
    batch.swap(gMainQueue);
  }
  for (auto& fn : batch) fn();
}

int pendingJobs() { return gPending.load(); }

}  // namespace async
