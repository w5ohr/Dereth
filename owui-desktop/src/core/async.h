// Worker pool plus a main-thread dispatch queue. Network work runs on a
// worker; anything that touches UI state is posted back and drained once per
// frame, so the rest of the app never needs a lock.
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace async {

void start(int threads = 6);
void shutdown();

// Queue a job on the shared pool.
void run(std::function<void()> job);

// Run a job on a thread of its own. Use for long-lived streams that would
// otherwise occupy a pool slot for minutes.
void runDetached(std::function<void()> job);

// Hand a callback to the main thread.
void post(std::function<void()> fn);

// Called once per frame from the UI thread.
void drain();

int pendingJobs();

}  // namespace async
