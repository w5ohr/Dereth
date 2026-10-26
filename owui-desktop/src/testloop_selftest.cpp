// `Open WebUI --testlooptest`: offline checks of runner detection, file
// mentions and the failure digest.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "core/common.h"
#include "core/testloop.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string names(const std::vector<testloop::Runner>& r) {
  std::string s;
  for (const auto& x : r) s += x.name + " ";
  return s;
}
}  // namespace

int runTestLoopSelfTest() {
  using namespace testloop;
  printf("Open WebUI desktop test-loop self-test\n\n");
  const std::string dir = "/tmp/owui-testloop-" + std::to_string(getpid());
  mkdir(dir.c_str(), 0755);
  mkdir((dir + "/tests").c_str(), 0755);
  mkdir((dir + "/src").c_str(), 0755);
  core::writeFile(dir + "/tests/test_app.py", "def test_x():\n    assert 1\n");
  core::writeFile(dir + "/src/app.py", "x = 1\n");
  core::writeFile(dir + "/package.json", "{\"scripts\": {\"test\": \"jest\"}}");
  core::writeFile(dir + "/Makefile", "all:\n\techo\ntest:\n\techo t\n");

  auto r = detect(dir, false);
  report("unittest when pytest is missing", names(r).find("unittest") != std::string::npos && names(r).find("pytest") == std::string::npos, names(r));
  r = detect(dir, true);
  report("pytest when available", names(r).find("pytest") != std::string::npos, names(r));
  report("npm test from package.json scripts", names(r).find("npm test") != std::string::npos);
  report("make test from a test: target", names(r).find("make test") != std::string::npos);
  report("nothing for an empty folder", detect("/tmp", false).empty() || detect("/tmp", false).size() < 3);

  const std::string output = "FAILED tests/test_app.py::test_x - AssertionError\n  File \"" + dir +
                             "/src/app.py\", line 1\n  File \"/usr/lib/python3/site-packages/x.py\"\n./tests/test_app.py:2: in test_x\nmissing/nope.py:3\n";
  auto files = mentionedFiles(output, dir);
  report("mentioned files: relative, existing, deduplicated, no site-packages",
         files.size() == 2 && files[0] == "tests/test_app.py" && files[1] == "src/app.py",
         [&] { std::string s; for (auto& f : files) s += f + " "; return s; }());

  const std::string big(30000, 'x');
  const std::string d = digest("HEAD" + big + "TAIL", 1000);
  report("digest keeps head and tail", d.rfind("HEAD", 0) == 0 && d.find("TAIL") != std::string::npos && d.size() < 1200);
  report("short output untouched", digest("short", 1000) == "short");

  report("pytest green", looksGreen("pytest", 0, "3 passed in 0.1s") && !looksGreen("pytest", 1, "1 failed") && !looksGreen("pytest", 0, "ERROR collecting"));
  report("unittest green", looksGreen("unittest", 0, "Ran 2 tests\n\nOK\n") && !looksGreen("unittest", 1, "FAILED (failures=1)"));
  report("other runners: exit code", looksGreen("npm test", 0, "") && !looksGreen("npm test", 1, ""));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
