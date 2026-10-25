#include "core/async.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace async {
namespace {

// The pool's state lives in a heap object that is never destroyed. A
// std::vector<std::thread> with static storage aborts the process at exit
// (std::terminate) if any worker is still joinable - which is what happened
// when a code path returned from main() without calling shutdown(). Leaked
// on purpose, the threads and the queues they wait on stay valid until the
// process is gone; shutdown() still stops and joins them when it is called.
struct State {
  std::vector<std::thread> workers;
  std::deque<std::function<void()>> jobs;
  std::mutex jobMutex;
  std::condition_variable jobCv;
  std::deque<std::function<void()>> mainQueue;
  std::mutex mainMutex;
};
State& state() {
  static State* s = new State();
  return *s;
}
std::atomic<bool> gRunning{false};
std::atomic<int> gPending{0};

#define gWorkers (state().workers)
#define gJobs (state().jobs)
#define gJobMutex (state().jobMutex)
#define gJobCv (state().jobCv)
#define gMainQueue (state().mainQueue)
#define gMainMutex (state().mainMutex)

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

#undef gWorkers
#undef gJobs
#undef gJobMutex
#undef gJobCv
#undef gMainQueue
#undef gMainMutex

}  // namespace async
