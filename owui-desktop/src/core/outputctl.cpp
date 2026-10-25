#include "outputctl.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace outputctl {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

// Rules in order: name -> body (continuation lines joined with a space).
std::vector<std::pair<std::string, std::string>> rules(const std::string& gbnf, std::string* error) {
  std::vector<std::pair<std::string, std::string>> out;
  std::istringstream in(gbnf);
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    // Strip a comment (a # outside quotes).
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '\\' && quoted) {
        ++i;
        continue;
      }
      if (line[i] == '"') quoted = !quoted;
      else if (line[i] == '#' && !quoted) {
        line = line.substr(0, i);
        break;
      }
    }
    const std::string t = trim(line);
    if (t.empty()) continue;
    const size_t def = t.find("::=");
    size_t i = 0;
    while (i < t.size() && identChar(t[i])) ++i;
    const bool isRule = def != std::string::npos && i > 0 && identStart(t[0]) && trim(t.substr(0, def)) == t.substr(0, i);
    if (isRule) {
      out.emplace_back(t.substr(0, i), trim(t.substr(def + 3)));
    } else if (!out.empty()) {
      out.back().second += " " + t;
    } else if (error) {
      *error = "line " + std::to_string(lineNo) + ": expected `name ::= ...`";
      return out;
    }
  }
  return out;
}

}  // namespace

Controls fromJson(const Json& raw) {
  Controls c;
  const Json j = raw.is_object() ? raw.value("outputControls", Json::object()) : Json::object();
  if (!j.is_object()) return c;
  const std::string mode = j.value("mode", c.mode);
  if (mode == "free" || mode == "json" || mode == "grammar") c.mode = mode;
  c.grammar = j.value("grammar", "");
  const std::string choice = j.value("toolChoice", c.toolChoice);
  if (choice == "auto" || choice == "none" || choice == "required" || (choice.rfind("tool:", 0) == 0 && choice.size() > 5)) c.toolChoice = choice;
  return c;
}

Json toJson(const Controls& c) { return Json{{"mode", c.mode}, {"grammar", c.grammar}, {"toolChoice", c.toolChoice}}; }

bool active(const Controls& c) { return c.mode != "free" || c.toolChoice != "auto"; }   // a chosen mode is kept even while its grammar is still being typed

std::string validateGrammar(const std::string& gbnf) {
  if (trim(gbnf).empty()) return "the grammar is empty";
  std::string error;
  const auto list = rules(gbnf, &error);
  if (!error.empty()) return error;
  if (list.empty()) return "no rules found";
  std::set<std::string> names;
  for (const auto& [name, body] : list) names.insert(name);
  if (!names.count("root")) return "no `root` rule - generation starts from root";
  for (const auto& [name, body] : list) {
    if (trim(body).empty()) return "rule `" + name + "` has an empty body";
    int quotes = 0, parens = 0, brackets = 0;
    bool quoted = false, inClass = false;
    for (size_t i = 0; i < body.size(); ++i) {
      const char ch = body[i];
      if (ch == '\\' && (quoted || inClass)) {
        ++i;
        continue;
      }
      if (quoted) {
        if (ch == '"') quoted = false, ++quotes;
        continue;
      }
      if (inClass) {
        if (ch == ']') inClass = false, --brackets;
        continue;
      }
      if (ch == '"') quoted = true, ++quotes;
      else if (ch == '[') inClass = true, ++brackets;
      else if (ch == '(') ++parens;
      else if (ch == ')') --parens;
      else if (identStart(ch)) {
        size_t j = i;
        while (j < body.size() && identChar(body[j])) ++j;
        const std::string ref = body.substr(i, j - i);
        if (!names.count(ref)) return "rule `" + name + "` refers to `" + ref + "`, which is not defined";
        i = j - 1;
      }
      if (parens < 0) return "rule `" + name + "`: unbalanced parentheses";
    }
    if (quoted) return "rule `" + name + "`: unclosed string";
    if (inClass) return "rule `" + name + "`: unclosed character class";
    if (parens != 0) return "rule `" + name + "`: unbalanced parentheses";
  }
  return "";
}

int ruleCount(const std::string& gbnf) {
  std::string error;
  return static_cast<int>(rules(gbnf, &error).size());
}

std::string forcedTool(const std::string& choice) { return choice.rfind("tool:", 0) == 0 ? choice.substr(5) : ""; }

Json openaiToolChoice(const std::string& choice) {
  const std::string tool = forcedTool(choice);
  if (!tool.empty()) return Json{{"type", "function"}, {"function", Json{{"name", tool}}}};
  if (choice == "none" || choice == "required") return choice;
  return "auto";
}

Json anthropicToolChoice(const std::string& choice) {
  const std::string tool = forcedTool(choice);
  if (!tool.empty()) return Json{{"type", "tool"}, {"name", tool}};
  if (choice == "required") return Json{{"type", "any"}};
  if (choice == "none") return Json();
  return Json{{"type", "auto"}};
}

std::string nudge(const Controls& c, const std::vector<std::string>& toolNames) {
  std::string out;
  const std::string tool = forcedTool(c.toolChoice);
  if (!tool.empty()) {
    out = "You must call the tool `" + tool + "` before answering; do not answer from memory first.";
  } else if (c.toolChoice == "required" && !toolNames.empty()) {
    out = "You must call at least one tool before answering (";
    for (size_t i = 0; i < toolNames.size(); ++i) out += (i ? ", " : "") + toolNames[i];
    out += "); do not answer from memory first.";
  }
  if (c.mode == "json") {
    if (!out.empty()) out += "\n";
    out += "Reply with a single JSON object and nothing else - no prose before or after it, no code fence.";
  }
  return out;
}

void applyToOllamaPayload(Json& payload, const Controls& c) {
  if (!payload.is_object()) return;
  if (c.mode == "json" && !payload.contains("format")) payload["format"] = "json";
  if (c.mode == "grammar" && !trim(c.grammar).empty()) payload["grammar"] = c.grammar;
  if (c.toolChoice == "none") {
    payload.erase("tools");
    payload.erase("tool_choice");
  } else if (c.toolChoice != "auto" && payload.contains("tools") && payload["tools"].is_array() && !payload["tools"].empty()) {
    payload["tool_choice"] = openaiToolChoice(c.toolChoice);
  }
}

std::string summary(const Controls& c) {
  std::vector<std::string> parts;
  if (c.mode == "json") parts.push_back("JSON");
  else if (c.mode == "grammar" && !trim(c.grammar).empty()) parts.push_back("GBNF (" + std::to_string(ruleCount(c.grammar)) + " rule" + (ruleCount(c.grammar) == 1 ? "" : "s") + ")");
  const std::string tool = forcedTool(c.toolChoice);
  if (!tool.empty()) parts.push_back("tool: " + tool);
  else if (c.toolChoice == "none") parts.push_back("no tools");
  else if (c.toolChoice == "required") parts.push_back("a tool first");
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
  return out;
}

}  // namespace outputctl
