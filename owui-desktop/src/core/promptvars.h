// Prompt variables: {{clipboard}}, {{selection}}, {{date}}, {{file:path}},
// {{input:Label|default}} and friends, expanded when a prompt is sent. The
// parsing and substitution live here, free of the app, so they can be tested
// offline; the app supplies a Context with the live values.
#pragma once

#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace promptvars {

struct Var {
  std::string raw;      // the whole "{{...}}" as written
  std::string name;     // lower-cased keyword: clipboard, date, file, input, ...
  std::string arg;      // after the colon: a path for file, a label for input
  std::string def;      // after the pipe: an input's default value
  size_t pos = 0;       // offset of raw in the text
};

// Every {{...}} in the text, in order. Unknown names are reported too (they
// are left untouched by expand), so callers can see them.
std::vector<Var> parse(const std::string& text);

// The input variables, one per distinct label, in first-seen order.
std::vector<Var> inputs(const std::vector<Var>& vars);

// Whether the text has anything expand() would change.
bool hasVariables(const std::string& text);

struct Context {
  std::function<std::string()> clipboard;                      // may be empty
  std::function<std::string()> selection;
  std::function<std::string(const std::string& path)> file;    // "" when unreadable
  std::string user;
  std::string model;
  std::string chatTitle;
  std::time_t now = 0;                                         // 0 = time(nullptr)
  std::map<std::string, std::string> inputs;                   // label -> value
};

struct Expansion {
  std::string text;
  std::vector<std::string> notes;    // what went in: "clipboard: 2.1 KB", "file x: not found"
};

// Substitutes every known variable once (text that comes out of the
// clipboard or a file is never scanned again). Inputs without a value in
// ctx.inputs take their default, or stay empty.
Expansion expand(const std::string& text, const Context& ctx);

// Names expand() understands, for help text.
const std::vector<std::string>& knownNames();

}  // namespace promptvars
