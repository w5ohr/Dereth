// Script tools (#87): shell, Python, AppleScript, JXA, Ruby, Perl or any
// executable the user drops into support/scripts/ with a small comment
// header becomes a tool the model can call. The header names the tool,
// describes it and declares its arguments as a schema; arguments arrive as
// JSON (last argv, OWUI_ARGS) and as OWUI_ARG_<NAME> variables; stdout is
// the result. Runs through native::runProcess, sandboxed like Run (no
// network, writes only inside the working folder) unless the header opts
// out. App-free; --scripttest covers parsing, argv building and a real run.
//
//   #!/bin/zsh
//   # owui-tool: disk_usage
//   # description: Disk usage of a folder, biggest entries first.
//   # param: path string required - Absolute folder path
//   # param: depth integer - Levels deep (default 1)
//   # timeout: 60
//   # sandbox: on
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace scripttools {

using Json = nlohmann::json;

struct Param {
  std::string name, type = "string", description;
  bool required = false;
  std::vector<std::string> enumValues;
};

struct Tool {
  std::string name;              // [a-z0-9_]+, what the model calls (qualified "scripts__<name>")
  std::string description;
  std::string path;              // the script file
  std::string file;              // its base name
  std::vector<std::string> runner;   // interpreter argv prefix ({"/bin/zsh"}, {"osascript","-l","JavaScript"}); empty = exec the file
  std::vector<Param> params;
  int timeoutSec = 60;
  bool sandbox = true;
  std::string error;             // set when the header is unusable; such a tool is listed but never offered
  Json schema() const;           // JSON schema for the arguments
};

// The header of one file. `content` is the file's text (only its first ~80 lines are read).
Tool parse(const std::string& path, const std::string& content);
// Every file in `dir` that carries an "owui-tool:" header, sorted by name.
std::vector<Tool> scan(const std::string& dir);
// "" when `args` satisfy the schema well enough to run, else what is wrong.
std::string validateArgs(const Tool& tool, const Json& args);
// What to execute: /usr/bin/env OWUI_ARGS=<json> OWUI_ARG_<NAME>=<value>... <runner> <path> <json>
std::vector<std::string> argv(const Tool& tool, const Json& args);
// The example scripts the UI can write into an empty folder: {file name, content}.
std::vector<std::pair<std::string, std::string>> examples();

}  // namespace scripttools
