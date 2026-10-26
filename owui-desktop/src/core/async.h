// Worker pool plus a main-thread dispatch queue. Network work runs on a
// worker; anything that touches UI state is posted back and drained once per
// frame, so the rest of the app never needs a lock.
#pragma once

#include <functional>
#include <cstdint>
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

// Windows (#121): each chat window has its own view of the app. A job queued
// while a window's view is current carries that window's token to its worker,
// and what it posts back runs inside that window again (through the runner),
// so a reply lands in the window that asked. Token 0 is the main window.
// Post to the main window's view whatever window is current.
void postMain(std::function<void()> fn);
uint64_t currentView();
void setCurrentView(uint64_t token);   // the calling thread's token
// Runs fn with the view `token` swapped in; false when that window is gone
// (the callback is then dropped).
void setViewRunner(std::function<bool(uint64_t token, const std::function<void()>& fn)> runner);

}  // namespace async
