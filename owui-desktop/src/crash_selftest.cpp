// `Open WebUI --crashtest`: real crashes in child processes (segfault, abort,
// uncaught exception, a joinable std::thread destroyed), each report read
// back; a macOS .ips parsed; the GitHub issue text checked for privacy.
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "core/crashreport.h"

extern char** environ;

// The child side: `--crashchild <kind> <folder>`. Exported and not inlined so
// the backtrace names it.
extern "C" __attribute__((noinline, visibility("default"))) void owuiCrashChildHere(const char* kind) {
  if (strcmp(kind, "segv") == 0) {
    volatile int* p = nullptr;
    *p = 42;
  } else if (strcmp(kind, "abort") == 0) {
    abort();
  } else if (strcmp(kind, "throw") == 0) {
    throw std::runtime_error("model list was empty");
  } else if (strcmp(kind, "thread") == 0) {
    std::thread t([] { std::this_thread::sleep_for(std::chrono::seconds(5)); });
    // t goes out of scope joinable -> std::terminate, today's exit crash
  }
}

int runCrashChild(int argc, const char* argv[]) {
  if (argc < 4) return 2;
  crashreport::install(argv[3], "test-1.0", true);
  crashreport::note("loaded 3 chats");
  crashreport::note("sending a message to qwen3:8b");
  owuiCrashChildHere(argv[2]);
  return 0;
}

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

int crashChild(const std::string& kind, const std::string& folder) {
  char exe[4096];
  uint32_t size = sizeof(exe);
  if (_NSGetExecutablePath(exe, &size) != 0) return -1;
  std::string k = kind, f = folder;
  const char* args[] = {exe, "--crashchild", k.c_str(), f.c_str(), nullptr};
  pid_t pid = -1;
  if (posix_spawn(&pid, exe, nullptr, nullptr, const_cast<char**>(args), environ) != 0) return -1;
  int status = 0;
  waitpid(pid, &status, 0);
  // The quiet handler exits normally with 128+signal; dying from the signal
  // itself (handler never ran) is reported as 1000+signal so the test can tell.
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1000 + WTERMSIG(status);
}

crashreport::Report onlyReport(const std::string& folder) {
  const auto list = crashreport::list(folder, "", "");
  return list.empty() ? crashreport::Report{} : list.front();
}
}  // namespace

int runCrashSelfTest() {
  using namespace crashreport;
  printf("Open WebUI desktop crash-reporter self-test\n\n");
  const std::string base = (std::filesystem::temp_directory_path() / "owui-crashtest").string();
  std::filesystem::remove_all(base);

  struct Case {
    const char* kind;
    int code;
    const char* signal;
    const char* reason;   // substring
  };
  const Case cases[] = {{"segv", 128 + 11, "SIGSEGV", "bad access at 0x"}, {"abort", 128 + 6, "SIGABRT", ""}, {"throw", 128 + 6, "SIGABRT", "uncaught exception: model list was empty"},
                        {"thread", 128 + 6, "SIGABRT", "std::terminate called"}};
  for (const Case& c : cases) {
    const std::string folder = base + "/" + c.kind;
    const int code = crashChild(c.kind, folder);
    const Report r = onlyReport(folder);
    bool namesChild = false;
    for (const std::string& f : r.frames) namesChild = namesChild || f.find("owuiCrashChildHere") != std::string::npos;
    report(std::string("crash child '") + c.kind + "': exits " + std::to_string(c.code) + ", report has the signal, reason, version, a backtrace naming the crashing function, the notes",
           code == c.code && r.signal == c.signal && r.reason.find(c.reason) != std::string::npos && r.version == "test-1.0" && namesChild && r.notes.size() == 2 &&
               r.notes[1] == "sending a message to qwen3:8b" && r.time > 0,
           "exit " + std::to_string(code) + ", " + r.signal + ", " + r.reason.substr(0, 60) + ", " + std::to_string(r.frames.size()) + " frames");
  }
  const Report seg = onlyReport(base + "/segv");
  report("summary names the crashing function, skipping the handler and libc frames", summary(seg).find("SIGSEGV - ") == 0 && summary(seg).find("in owuiCrashChildHere") != std::string::npos, summary(seg));

  // A macOS .ips shaped like the real one from today's exit crash.
  const std::string ips =
      R"js({"app_name":"Open WebUI","timestamp":"2026-10-25 16:47:32.00 -0500","app_version":"1.1","bug_type":"309"})js"
      "\n"
      R"js({"captureTime":"2026-10-25 16:47:29.6627 -0500","procPath":"\/Users\/david\/Documents\/x\/Open WebUI.app\/Contents\/MacOS\/Open WebUI","exception":{"type":"EXC_CRASH","signal":"SIGABRT"},"termination":{"indicator":"Abort trap: 6"},"asi":{"libsystem_c.dylib":["abort() called"]},"faultingThread":0,"threads":[{"frames":[{"symbol":"__pthread_kill"},{"symbol":"pthread_kill"},{"symbol":"abort"},{"symbol":"__abort_message"},{"symbol":"demangling_terminate_handler()"},{"symbol":"std::__terminate(void (*)())"},{"symbol":"std::terminate()"},{"symbol":"std::__1::vector<std::__1::thread, std::__1::allocator<std::__1::thread>>::~vector[abi:nqe210106]()"},{"symbol":"__cxa_finalize_ranges"},{"symbol":"exit"}]}]})js";
  const Report m = parseIps(ips, "/x/Open WebUI-2026-10-25-164732.ips");
  report("parseIps: version, signal, reason, the faulting thread's symbols, time", m.source == "macos" && m.version == "1.1" && m.signal == "SIGABRT" && m.reason == "abort() called" && m.frames.size() == 10 &&
                                                                                      m.frames[7].find("vector<std::__1::thread") != std::string::npos && m.time > 1700000000);
  report("summary of the .ips points at the vector<thread> destructor", summary(m).find("in std::__1::vector<std::__1::thread") != std::string::npos && summary(m).find("(macOS report)") != std::string::npos, summary(m));

  Report withNotes = seg;
  withNotes.notes = {"sending /Users/david/secret.txt to the model"};
  withNotes.frames.insert(withNotes.frames.begin(), "/Users/david/build/thing");
  const std::string body = issueBody(withNotes, "/Users/david");
  report("issue text: version, signal, frames; no notes, home paths as ~", body.find("test-1.0") != std::string::npos && body.find("SIGSEGV") != std::string::npos && body.find("secret.txt") == std::string::npos &&
                                                                                 body.find("/Users/david") == std::string::npos && body.find("~/build/thing") != std::string::npos);
  const std::string url = issueUrl("w5ohr/Dereth", m, "/Users/david");
  report("issueUrl: the repo's new-issue page, encoded, labelled, not over 8 KB", url.rfind("https://github.com/w5ohr/Dereth/issues/new?title=Crash%3A%20SIGABRT", 0) == 0 && url.find("&labels=crash") != std::string::npos &&
                                                                                     url.find(' ') == std::string::npos && url.size() < 8000);
  report("redactHome", redactHome("/Users/david/a and /Users/david/b", "/Users/david") == "~/a and ~/b" && redactHome("x", "") == "x");

  // list(): ours and macOS's, newest first; newerThan.
  const std::string diag = base + "/diag";
  std::filesystem::create_directories(diag);
  std::ofstream(diag + "/Open WebUI-2026-10-25-164732.ips") << ips;
  std::ofstream(diag + "/Other App-2026-10-25-100000.ips") << ips;
  const auto all = list(base + "/segv", diag, "Open WebUI");
  report("list: our report and the matching .ips (not other apps'), newest first; newerThan filters", all.size() == 2 && all[0].time >= all[1].time && newerThan(all, all[0].time).empty() && newerThan(all, 0).size() == 2);
  std::filesystem::remove_all(base);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
