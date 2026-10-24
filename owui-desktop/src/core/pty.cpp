#include "core/pty.h"

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "core/common.h"

namespace core {

Pty::~Pty() { stop(); }

bool Pty::start(const std::vector<std::string>& argv, const std::string& cwd, int cols, int rows,
                std::string* error) {
  if (argv.empty()) {
    if (error) *error = "no command";
    return false;
  }
  stop();

  struct winsize size{};
  size.ws_col = static_cast<unsigned short>(cols);
  size.ws_row = static_cast<unsigned short>(rows);

  int fd = -1;
  const pid_t pid = forkpty(&fd, nullptr, nullptr, &size);
  if (pid < 0) {
    if (error) *error = std::string("forkpty: ") + strerror(errno);
    return false;
  }
  if (pid == 0) {
    // Child: an interactive shell expects these, and a login shell sets PATH
    // from the user's profile, which a GUI app doesn't inherit.
    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) chdir(getenv("HOME") ? getenv("HOME") : "/");
    }
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    if (!getenv("LANG")) setenv("LANG", "en_US.UTF-8", 1);
    setenv("OWUI_TERMINAL", "1", 1);
    std::vector<std::string> store = argv;
    std::vector<char*> args;
    for (auto& a : store) args.push_back(a.data());
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }

  fd_ = fd;
  pid_ = pid;
  exitCode_ = -1;
  alive_.store(true);
  reader_ = std::thread([this] { readLoop(); });
  core::debugLog("pty: started %s (pid %d, %dx%d)", argv.front().c_str(), pid, cols, rows);
  return true;
}

void Pty::readLoop() {
  char buffer[16384];
  while (true) {
    const ssize_t n = ::read(fd_, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.append(buffer, static_cast<size_t>(n));
  }
  int status = 0;
  if (pid_ > 0 && waitpid(pid_, &status, 0) == pid_) {
    exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
  }
  alive_.store(false);
}

void Pty::write(const std::string& bytes) {
  if (fd_ < 0 || !alive_.load()) return;
  size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t n = ::write(fd_, bytes.data() + offset, bytes.size() - offset);
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      return;
    }
    offset += static_cast<size_t>(n);
  }
}

void Pty::resize(int cols, int rows) {
  if (fd_ < 0) return;
  struct winsize size{};
  size.ws_col = static_cast<unsigned short>(cols);
  size.ws_row = static_cast<unsigned short>(rows);
  ioctl(fd_, TIOCSWINSZ, &size);
}

std::string Pty::drain() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string out;
  out.swap(pending_);
  return out;
}

void Pty::stop() {
  if (pid_ > 0 && alive_.load()) {
    kill(pid_, SIGHUP);
    for (int i = 0; i < 10 && alive_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (alive_.load()) kill(pid_, SIGKILL);
  }
  if (fd_ >= 0) {
    ::close(fd_);   // unblocks the reader
    fd_ = -1;
  }
  if (reader_.joinable()) reader_.join();
  pid_ = -1;
  alive_.store(false);
}

}  // namespace core
