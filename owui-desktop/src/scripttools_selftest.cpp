// `Open WebUI --scripttest`: header parsing, schemas, argv building and a
// real sandboxed run of a shell script tool.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/common.h"
#include "core/scripttools.h"
#include "native.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runScriptToolsSelfTest() {
  using namespace scripttools;
  printf("Open WebUI desktop script-tools self-test\n\n");

  Tool sh = parse("/x/disk_usage.sh",
                  "#!/bin/zsh\n# owui-tool: Disk_Usage\n# description: Disk usage of a folder.\n"
                  "# param: path string required - Absolute folder path\n# param: depth integer - Levels deep\n"
                  "# param: unit enum(k|m|g) - Units\n# timeout: 90\n# sandbox: off\ndu -h\n");
  report("shell header: name lowered, params with types / required / enum, timeout, sandbox off, zsh runner",
         sh.error.empty() && sh.name == "disk_usage" && sh.params.size() == 3 && sh.params[0].required && sh.params[0].type == "string" &&
             sh.params[0].description == "Absolute folder path" && !sh.params[1].required && sh.params[1].type == "integer" &&
             sh.params[2].enumValues.size() == 3 && sh.timeoutSec == 90 && !sh.sandbox && sh.runner == std::vector<std::string>{"/bin/zsh"},
         sh.error);
  const Json schema = sh.schema();
  report("schema: object with properties, required list, enum", schema["type"] == "object" && schema["properties"]["depth"]["type"] == "integer" &&
                                                                     schema["required"] == Json::array({"path"}) && schema["properties"]["unit"]["enum"][1] == "m");

  Tool js = parse("/x/front.js", "// owui-tool: front_app\n// description: Front app.\nfunction run(argv) { return 'x'; }\n");
  report("JXA header: osascript -l JavaScript runner, defaults (60 s, sandboxed, no params)",
         js.error.empty() && js.runner == std::vector<std::string>{"osascript", "-l", "JavaScript"} && js.timeoutSec == 60 && js.sandbox && js.params.empty());
  Tool as = parse("/x/say.applescript", "-- owui-tool: say_it\n-- description: Says it.\n-- param: text string required - Words\non run argv\nend run\n");
  report("AppleScript header with -- comments", as.error.empty() && as.runner == std::vector<std::string>{"osascript"} && as.params.size() == 1);
  Tool py = parse("/x/w.py", "#!/usr/bin/env python3\n# owui-tool: word_count\n# param: text string required The text to measure\nprint(1)\n");
  report("python: loose param description without the dash; description defaults to the file",
         py.error.empty() && py.runner == std::vector<std::string>{"python3"} && py.params[0].description == "The text to measure" && py.description == "Runs w.py");
  Tool bad = parse("/x/bad.sh", "# owui-tool: Bad Name!\n");
  report("bad name is an error, tool still parsed", !bad.error.empty() && bad.name == "bad name!");
  Tool none = parse("/x/plain.sh", "#!/bin/zsh\necho hi\n");
  report("no header -> error", none.error == "no owui-tool header");
  Tool custom = parse("/x/thing.txt", "# owui-tool: thing\n# runner: /usr/bin/env node\n");
  report("runner override", custom.runner == std::vector<std::string>{"/usr/bin/env", "node"});

  report("validateArgs: missing required, enum, ok", validateArgs(sh, Json{{"depth", 2}}).find("missing") != std::string::npos &&
                                                        validateArgs(sh, Json{{"path", "/"}, {"unit", "x"}}).find("one of") != std::string::npos &&
                                                        validateArgs(sh, Json{{"path", "/"}, {"unit", "g"}}).empty() &&
                                                        !validateArgs(sh, Json::array()).empty());
  std::vector<std::string> av = argv(sh, Json{{"path", "/tmp"}, {"depth", 2}});
  report("argv: env prefix, OWUI_ARGS, OWUI_ARG_<NAME>, runner, path, json last",
         av.size() == 8 && av[0] == "/usr/bin/env" && av[1].rfind("OWUI_ARGS={", 0) == 0 && av[2] == "OWUI_TOOL=disk_usage" &&
             av[3] == "OWUI_ARG_DEPTH=2" && av[4] == "OWUI_ARG_PATH=/tmp" && av[5] == "/bin/zsh" && av[6] == "/x/disk_usage.sh" && av[7].front() == '{',
         core::join(av, " | "));

  // A real folder: scan, duplicates, and a sandboxed run.
  namespace fs = std::filesystem;
  const std::string dir = (fs::temp_directory_path() / ("owui-scripttest-" + std::to_string(getpid()))).string();
  fs::create_directories(dir);
  for (const auto& [file, content] : examples()) {
    core::writeFile(dir + "/" + file, content);
    chmod((dir + "/" + file).c_str(), 0700);
  }
  core::writeFile(dir + "/echo_args.sh", "# owui-tool: echo_args\n# description: echoes\n# param: who string required - name\n"
                                         "echo \"hello $OWUI_ARG_WHO from $OWUI_TOOL\"\necho \"json: $1\"\necho \"env: $OWUI_ARGS\"\n");
  core::writeFile(dir + "/dup.sh", "# owui-tool: echo_args\n");
  core::writeFile(dir + "/notes.txt", "just a note");
  core::writeFile(dir + "/slow.sh", "# owui-tool: slow\n# timeout: 1\nsleep 4\necho late\n");
  core::writeFile(dir + "/fails.sh", "# owui-tool: fails\necho boom >&2\nexit 3\n");
  core::writeFile(dir + "/writes_outside.sh", "# owui-tool: writes_outside\ntouch \"$HOME/owui-scripttest-should-not-exist\" && echo wrote || echo denied\n");
  std::vector<Tool> found = scan(dir);
  size_t good = 0;
  std::string dupError;
  for (const Tool& t : found) {
    if (t.error.empty()) ++good;
    if (t.file == "dup.sh") dupError = t.error;
  }
  report("scan: header files only, sorted, duplicate flagged", found.size() == 8 && good == 7 && dupError.find("duplicate") != std::string::npos &&
                                                                   found.front().name <= found.back().name, std::to_string(found.size()) + " found, " + std::to_string(good) + " good");

  auto toolNamed = [&](const std::string& n) {
    for (const Tool& t : found) {
      if (t.name == n && t.error.empty()) return t;
    }
    return Tool();
  };
  const Tool echo = toolNamed("echo_args");
  native::ProcessResult run = native::runProcess(argv(echo, Json{{"who", "Ada"}}), dir, echo.timeoutSec, echo.sandbox);
  report("run (sandboxed zsh): args arrive by env, argv and OWUI_ARGS",
         run.exitCode == 0 && !run.timedOut && run.output.find("hello Ada from echo_args") != std::string::npos &&
             run.output.find("json: {\"who\":\"Ada\"}") != std::string::npos && run.output.find("env: {\"who\":\"Ada\"}") != std::string::npos,
         run.output.substr(0, 200));
  const Tool wc = toolNamed("word_count");
  native::ProcessResult py2 = native::runProcess(argv(wc, Json{{"text", "one two three\nfour"}}), dir, wc.timeoutSec, wc.sandbox);
  report("run (python example): JSON result", py2.exitCode == 0 && py2.output.find("\"words\": 4") != std::string::npos && py2.output.find("\"lines\": 2") != std::string::npos, py2.output.substr(0, 200));
  const Tool slow = toolNamed("slow");
  native::ProcessResult late = native::runProcess(argv(slow, Json::object()), dir, slow.timeoutSec, slow.sandbox);
  report("run: timeout kills the script", late.timedOut && late.output.find("late") == std::string::npos);
  const Tool fails = toolNamed("fails");
  native::ProcessResult failed = native::runProcess(argv(fails, Json::object()), dir, fails.timeoutSec, fails.sandbox);
  report("run: non-zero exit and stderr captured", failed.exitCode == 3 && failed.output.find("boom") != std::string::npos);
  const Tool outside = toolNamed("writes_outside");
  native::ProcessResult wo = native::runProcess(argv(outside, Json::object()), dir, outside.timeoutSec, outside.sandbox);
  const std::string stray = std::string(getenv("HOME") ? getenv("HOME") : "") + "/owui-scripttest-should-not-exist";
  struct stat st {};
  const bool strayExists = stat(stray.c_str(), &st) == 0;
  if (strayExists) unlink(stray.c_str());
  report("run: the sandbox denies writes outside the working folder", wo.output.find("denied") != std::string::npos && !strayExists, wo.output.substr(0, 120));

  std::error_code ec;
  fs::remove_all(dir, ec);
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
