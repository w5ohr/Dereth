// A pseudo-terminal running a child process (the user's shell). Output is
// collected by a reader thread and handed over in drain(); input and resizes
// go straight to the pty. Nothing here parses escape sequences - that is
// ui/terminal.h.
#pragma once

#include <sys/types.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace core {

class Pty {
 public:
  Pty() = default;
  ~Pty();
  Pty(const Pty&) = delete;
  Pty& operator=(const Pty&) = delete;

  bool start(const std::vector<std::string>& argv, const std::string& cwd, int cols, int rows,
             std::string* error);
  void write(const std::string& bytes);
  void resize(int cols, int rows);
  // Bytes received since the previous call.
  std::string drain();
  bool alive() const { return alive_.load(); }
  int exitCode() const { return exitCode_; }
  pid_t pid() const { return pid_; }
  // Hangs the child up, kills it if it lingers, and joins the reader.
  void stop();

 private:
  void readLoop();

  int fd_ = -1;
  pid_t pid_ = -1;
  std::thread reader_;
  std::mutex mutex_;
  std::string pending_;
  std::atomic<bool> alive_{false};
  int exitCode_ = -1;
};

}  // namespace core
