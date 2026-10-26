// `Open WebUI --audittest`: offline checks of the audit log.
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "core/auditlog.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runAuditSelfTest() {
  using namespace auditlog;
  printf("Open WebUI desktop audit-log self-test\n\n");
  std::string host, path;
  splitUrl("https://user:pw@api.example.com:8443/v1/chat/completions?key=SECRET#frag", &host, &path);
  report("splitUrl strips credentials, query and fragment", host == "api.example.com:8443" && path == "/v1/chat/completions", host + " " + path);
  splitUrl("http://localhost:3001", &host, &path);
  report("splitUrl bare host", host == "localhost:3001" && path == "/");

  const std::string file = "/tmp/owui-audittest-" + std::to_string(getpid()) + ".jsonl";
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  Record a{now - 3 * 86400, "http", "localhost:3001", "POST", "/api/chat/completions", "", 200, 5000, 20000, 1200.0, true};
  Record b{now, "http", "api.openai.com", "POST", "/v1/chat/completions", "", 401, 800, 120, 300.0, false};
  Record c{now, "tool", "calc", "add", "calc__add", "allowed (always)", 0, 0, 0, 0.0, true};
  Record d{now, "tool", "calc", "fail", "calc__fail", "denied (user)", 0, 0, 0, 0.0, false};
  for (const Record& r : {a, b, c, d}) append(file, r);
  auto all = load(file, 0);
  report("round-trip", all.size() == 4 && all[1].status == 401 && !all[1].ok && all[3].detail == "denied (user)");
  report("sinceDays", load(file, 1).size() == 3);
  auto hosts = byHost(all);
  report("byHost: http only, most traffic first, failures counted",
         hosts.size() == 2 && hosts[0].host == "localhost:3001" && hosts[1].failures == 1 && hosts[0].bytesIn == 20000);
  const std::string text = csv(all);
  report("csv rows and quoting", text.rfind("time,kind,host,", 0) == 0 && std::count(text.begin(), text.end(), '\n') == 5 && text.find("\"denied (user)\"") != std::string::npos);
  report("json export", json(all).find("\"path\": \"calc__add\"") != std::string::npos);
  for (int i = 0; i < 30; ++i) append(file, c);
  trim(file, 20, 10);
  report("trim keeps the newest lines", load(file, 0).size() == 10);
  clear(file);
  report("clear", load(file, 0).empty());
  unlink(file.c_str());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
