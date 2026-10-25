// Crash reporter (#130): signal and std::terminate handlers that write a
// plain-text report (version, signal, faulting address, the uncaught
// exception, a backtrace and the last log lines) into a folder, then let the
// process die as before so macOS still writes its own .ips report. Plus the
// readers for both kinds of report, and the short, path-free text that may
// go into a GitHub issue. App-free; --crashtest covers it, crashing child
// processes for real.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace crashreport {

using Json = nlohmann::json;

// Install once, early. `quiet` exits with 128+signal after writing instead of
// re-raising (for tests: no macOS report, no "quit unexpectedly" dialog).
void install(const std::string& folder, const std::string& version, bool quiet = false);
// Recent activity kept in a fixed ring (no allocation); written into the
// report, never into anything that leaves the machine.
void note(const char* line);

struct Report {
  std::string path;
  std::string source;      // "app" (ours) | "macos" (.ips)
  int64_t time = 0;        // unix seconds
  std::string version;
  std::string signal;      // "SIGSEGV", "SIGABRT", ...
  std::string reason;      // "uncaught exception: ...", "abort() called", the faulting address
  std::vector<std::string> frames;   // symbol names, top first
  std::vector<std::string> notes;    // ours only
  std::string text;        // the whole file
};
Report parseAppReport(const std::string& text, const std::string& path = "");
Report parseIps(const std::string& text, const std::string& path = "");   // macOS .ips (header line + JSON body)
// Ours (*.crash in `folder`) and macOS's (<process>-*.ips in `diagnostics`),
// newest first.
std::vector<Report> list(const std::string& folder, const std::string& diagnostics, const std::string& process);
std::vector<Report> newerThan(const std::vector<Report>& reports, int64_t seen);
std::string summary(const Report& r);              // "SIGSEGV - 2026-10-25 17:02 - in crashChild"
std::string issueTitle(const Report& r);
std::string issueBody(const Report& r, const std::string& home);   // no notes, home paths as ~, top 12 frames
std::string issueUrl(const std::string& repo, const Report& r, const std::string& home);   // https://github.com/<repo>/issues/new?...
std::string redactHome(const std::string& text, const std::string& home);
const char* signalName(int sig);

}  // namespace crashreport
