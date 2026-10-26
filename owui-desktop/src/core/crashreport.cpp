#include "crashreport.h"

#include <cxxabi.h>
#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace crashreport {
namespace {

// Everything the handler touches is preallocated: no malloc, no locks.
char gFolder[1024];
char gVersion[64];
bool gQuiet = false;
std::atomic<bool> gInstalled{false};
std::atomic<int> gInHandler{0};
constexpr int kNotes = 40;
constexpr int kNoteLen = 200;
char gNotes[kNotes][kNoteLen];
std::atomic<unsigned> gNoteNext{0};
char gAltStack[64 * 1024];
std::terminate_handler gPrevTerminate = nullptr;

void put(int fd, const char* s) {
  if (s) (void)!::write(fd, s, strlen(s));
}

void putNum(int fd, unsigned long long v, int base = 10) {
  char buf[32];
  int i = 31;
  buf[i] = 0;
  if (v == 0) buf[--i] = '0';
  while (v && i > 0) {
    const int d = static_cast<int>(v % base);
    buf[--i] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
    v /= base;
  }
  put(fd, buf + i);
}

int openReport(time_t now) {
  char path[1200];
  // folder/crash-<unix seconds>-<pid>.crash, built without snprintf.
  size_t n = strlen(gFolder);
  memcpy(path, gFolder, n);
  const char* mid = "/crash-";
  memcpy(path + n, mid, 7);
  n += 7;
  char num[32];
  int i = 31;
  num[i] = 0;
  unsigned long long v = static_cast<unsigned long long>(now);
  if (!v) num[--i] = '0';
  while (v) num[--i] = static_cast<char>('0' + v % 10), v /= 10;
  memcpy(path + n, num + i, 31 - i);
  n += 31 - i;
  path[n++] = '-';
  i = 31;
  v = static_cast<unsigned long long>(getpid());
  while (v) num[--i] = static_cast<char>('0' + v % 10), v /= 10;
  memcpy(path + n, num + i, 31 - i);
  n += 31 - i;
  memcpy(path + n, ".crash", 7);
  return ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
}

void writeReport(int sig, const char* reason, void* address) {
  const time_t now = time(nullptr);
  const int fd = openReport(now);
  if (fd < 0) return;
  put(fd, "Open WebUI crashed\n");
  put(fd, "Version: ");
  put(fd, gVersion);
  put(fd, "\nTime: ");
  putNum(fd, static_cast<unsigned long long>(now));
  put(fd, "\nSignal: ");
  put(fd, signalName(sig));
  put(fd, "\nThread: ");
  put(fd, pthread_main_np() ? "main" : "background");
  if (sig == SIGSEGV || sig == SIGBUS) {   // the faulting address (0 for a null dereference)
    put(fd, "\nAddress: 0x");
    putNum(fd, reinterpret_cast<unsigned long long>(address), 16);
  }
  if (reason && *reason) {
    put(fd, "\nReason: ");
    put(fd, reason);
  }
  put(fd, "\n\nBacktrace:\n");
  void* frames[64];
  const int count = backtrace(frames, 64);
  backtrace_symbols_fd(frames, count, fd);
  put(fd, "\nRecent activity:\n");
  const unsigned next = gNoteNext.load();
  for (unsigned k = 0; k < kNotes; ++k) {
    const char* line = gNotes[(next + k) % kNotes];
    if (!line[0]) continue;
    put(fd, "  ");
    put(fd, line);
    put(fd, "\n");
  }
  put(fd, "End of report\n");
  ::close(fd);
}

void finish(int sig) {
  if (gQuiet) _exit(128 + sig);
  // Let the process die the way it would have: macOS writes its .ips report.
  signal(sig, SIG_DFL);
  raise(sig);
  _exit(128 + sig);
}

void onSignal(int sig, siginfo_t* info, void*) {
  if (gInHandler.fetch_add(1) == 0) writeReport(sig, nullptr, info ? info->si_addr : nullptr);
  finish(sig);
}

void onTerminate() {
  static char reason[512];
  reason[0] = 0;
  if (std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& x) {
      snprintf(reason, sizeof(reason), "uncaught exception: %s", x.what());
    } catch (...) {
      snprintf(reason, sizeof(reason), "uncaught exception of an unknown type");
    }
  } else {
    snprintf(reason, sizeof(reason), "std::terminate called (a joinable std::thread destroyed, a throwing destructor, ...)");
  }
  if (gInHandler.fetch_add(1) == 0) writeReport(SIGABRT, reason, nullptr);
  if (gQuiet) _exit(128 + SIGABRT);
  signal(SIGABRT, SIG_DFL);
  abort();
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string fmtTime(int64_t t) {
  char buf[32];
  const time_t tt = static_cast<time_t>(t);
  struct tm tmv;
  localtime_r(&tt, &tmv);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
  return buf;
}

}  // namespace

const char* signalName(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    case SIGTRAP: return "SIGTRAP";
    default: return "signal";
  }
}

void install(const std::string& folder, const std::string& version, bool quiet) {
  if (gInstalled.exchange(true)) return;
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);   // the whole path: the handler can't create folders later
  strlcpy(gFolder, folder.c_str(), sizeof(gFolder));
  strlcpy(gVersion, version.c_str(), sizeof(gVersion));
  gQuiet = quiet;
  stack_t ss{};
  ss.ss_sp = gAltStack;
  ss.ss_size = sizeof(gAltStack);
  sigaltstack(&ss, nullptr);   // a stack overflow still gets a report
  struct sigaction sa{};
  sa.sa_sigaction = onSignal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) sigaction(sig, &sa, nullptr);
  gPrevTerminate = std::set_terminate(onTerminate);
}

void note(const char* line) {
  if (!line) return;
  const unsigned slot = gNoteNext.fetch_add(1) % kNotes;
  strlcpy(gNotes[slot], line, kNoteLen);
}

Report parseAppReport(const std::string& text, const std::string& path) {
  Report r;
  r.path = path;
  r.source = "app";
  r.text = text;
  std::istringstream in(text);
  std::string line;
  enum { Header, Frames, Notes } section = Header;
  // "5   Open WebUI   0x0000000104d72400 owuiCrashChildHere + 280" - image names can contain spaces.
  static const std::regex frameRe(R"(^\d+\s+(.+?)\s+0x[0-9a-fA-F]+\s+(.+?)\s+\+\s+\d+\s*$)");
  while (std::getline(in, line)) {
    if (line == "Backtrace:") {
      section = Frames;
      continue;
    }
    if (line == "Recent activity:") {
      section = Notes;
      continue;
    }
    if (line == "End of report") break;
    if (section == Header) {
      const size_t colon = line.find(": ");
      if (colon == std::string::npos) continue;
      const std::string key = line.substr(0, colon), value = line.substr(colon + 2);
      if (key == "Version") r.version = value;
      else if (key == "Time") r.time = std::atoll(value.c_str());
      else if (key == "Signal") r.signal = value;
      else if (key == "Reason") r.reason = value;
      else if (key == "Address" && r.reason.empty()) r.reason = "bad access at " + value;
    } else if (section == Frames) {
      std::smatch m;
      if (std::regex_match(line, m, frameRe)) {
        std::string name = m[2].str();
        int status = 0;
        if (char* d = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status)) {
          if (status == 0) name = d;
          free(d);
        }
        r.frames.push_back(name);
      }
    } else if (!line.empty()) {
      r.notes.push_back(line.size() > 2 ? line.substr(2) : line);
    }
  }
  // The handler's own frames are noise.
  r.frames.erase(std::remove_if(r.frames.begin(), r.frames.end(), [](const std::string& f) {
                   return f.find("crashreport::") != std::string::npos || f == "_sigtramp" || f.find("__cxa_get_") == 0 || f.find("std::__terminate") == 0;
                 }),
                 r.frames.end());
  return r;
}

Report parseIps(const std::string& text, const std::string& path) {
  Report r;
  r.path = path;
  r.source = "macos";
  r.text = text;
  const size_t nl = text.find('\n');
  if (nl == std::string::npos) return r;
  const Json header = Json::parse(text.substr(0, nl), nullptr, false);
  const Json body = Json::parse(text.substr(nl + 1), nullptr, false);
  if (header.is_object()) r.version = header.value("app_version", "");
  if (!body.is_object()) return r;
  const Json exc = body.value("exception", Json::object());
  r.signal = exc.value("signal", exc.value("type", ""));
  const Json asi = body.value("asi", Json::object());
  for (auto it = asi.begin(); it != asi.end(); ++it) {
    if (it->is_array() && !it->empty() && (*it)[0].is_string()) r.reason = (*it)[0].get<std::string>();
  }
  if (r.reason.empty()) r.reason = body.value("termination", Json::object()).value("indicator", "");
  const std::string capture = body.value("captureTime", "");
  struct tm tmv{};
  if (strptime(capture.c_str(), "%Y-%m-%d %H:%M:%S", &tmv)) {
    tmv.tm_isdst = -1;
    r.time = static_cast<int64_t>(mktime(&tmv));
  }
  const int faulting = body.value("faultingThread", 0);
  const Json threads = body.value("threads", Json::array());
  if (threads.is_array() && faulting >= 0 && faulting < static_cast<int>(threads.size())) {
    for (const auto& f : threads[static_cast<size_t>(faulting)].value("frames", Json::array())) {
      if (f.contains("symbol") && f["symbol"].is_string()) r.frames.push_back(f["symbol"].get<std::string>());
    }
  }
  return r;
}

std::vector<Report> list(const std::string& folder, const std::string& diagnostics, const std::string& process) {
  std::vector<Report> out;
  auto scan = [&](const std::string& dir, auto pick) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      pick(dir + "/" + name, name);
    }
    closedir(d);
  };
  scan(folder, [&](const std::string& p, const std::string& name) {
    if (name.size() > 6 && name.compare(name.size() - 6, 6, ".crash") == 0) out.push_back(parseAppReport(readFile(p), p));
  });
  if (!diagnostics.empty()) {
    scan(diagnostics, [&](const std::string& p, const std::string& name) {
      if (name.rfind(process + "-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".ips") == 0) out.push_back(parseIps(readFile(p), p));
    });
  }
  std::stable_sort(out.begin(), out.end(), [](const Report& a, const Report& b) { return a.time > b.time; });
  return out;
}

std::vector<Report> newerThan(const std::vector<Report>& reports, int64_t seen) {
  std::vector<Report> out;
  for (const Report& r : reports) {
    if (r.time > seen) out.push_back(r);
  }
  return out;
}

std::string summary(const Report& r) {
  std::string s = (r.signal.empty() ? std::string("crash") : r.signal) + " - " + (r.time ? fmtTime(r.time) : std::string("unknown time"));
  for (const std::string& f : r.frames) {
    if (f.find("pthread_kill") != std::string::npos || f == "abort" || f.find("__abort") != std::string::npos || f.find("terminate") != std::string::npos ||
        f.find("demangling") != std::string::npos || f.find("raise") != std::string::npos || f.find("backtrace") != std::string::npos) {
      continue;
    }
    s += " - in " + f;
    break;
  }
  if (r.source == "macos") s += " (macOS report)";
  return s;
}

std::string redactHome(const std::string& text, const std::string& home) {
  if (home.size() < 2) return text;
  std::string out;
  size_t pos = 0;
  for (;;) {
    const size_t at = text.find(home, pos);
    if (at == std::string::npos) break;
    out += text.substr(pos, at - pos) + "~";
    pos = at + home.size();
  }
  return out + text.substr(pos);
}

std::string issueTitle(const Report& r) {
  std::string t = "Crash: " + (r.signal.empty() ? std::string("crash") : r.signal);
  for (const std::string& f : r.frames) {
    if (f.find("pthread_kill") != std::string::npos || f == "abort" || f.find("__abort") != std::string::npos || f.find("terminate") != std::string::npos || f.find("demangling") != std::string::npos) continue;
    t += " in " + (f.size() > 80 ? f.substr(0, 80) + "..." : f);
    break;
  }
  return t;
}

std::string issueBody(const Report& r, const std::string& home) {
  std::string b = "**Open WebUI desktop " + (r.version.empty() ? std::string("?") : r.version) + "** crashed with **" + (r.signal.empty() ? std::string("a crash") : r.signal) + "**";
  if (!r.reason.empty()) b += " (" + r.reason.substr(0, 300) + ")";
  b += ".\n\nTop of the stack:\n```\n";
  for (size_t i = 0; i < r.frames.size() && i < 12; ++i) b += r.frames[i].substr(0, 160) + "\n";
  b += "```\n\n(" + std::string(r.source == "macos" ? "From the macOS crash report." : "From the app's crash report.") +
       " Recent activity and the full report stay on the Mac; attach the file if it helps.)\n";
  return redactHome(b, home);
}

std::string issueUrl(const std::string& repo, const Report& r, const std::string& home) {
  return "https://github.com/" + repo + "/issues/new?title=" + urlEncode(issueTitle(r)) + "&body=" + urlEncode(issueBody(r, home)) + "&labels=crash";
}

}  // namespace crashreport
