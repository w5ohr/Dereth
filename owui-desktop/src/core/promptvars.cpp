#include "promptvars.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "common.h"

namespace promptvars {
namespace {

// {{ name }}, {{ name:arg }}, {{ name:arg|default }} - whitespace tolerant.
const std::regex& pattern() {
  static const std::regex re(R"(\{\{\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([^}|]*?))?\s*(?:\|\s*([^}]*?))?\s*\}\})");
  return re;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Open WebUI's upper-case names map onto the short ones.
std::string canonical(const std::string& name) {
  static const std::map<std::string, std::string> aliases = {
      {"current_date", "date"},       {"current_time", "time"},
      {"current_datetime", "datetime"}, {"current_weekday", "weekday"},
      {"current_timezone", "timezone"}, {"user_name", "user"},
      {"username", "user"},           {"chat_title", "title"},
      {"input", "input"},             {"prompt", "input"},
  };
  const std::string key = lower(name);
  auto it = aliases.find(key);
  return it == aliases.end() ? key : it->second;
}

std::string sizeText(size_t bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
  return buf;
}

std::string timeText(std::time_t t, const char* format) {
  std::tm local{};
  localtime_r(&t, &local);
  char buf[64];
  if (std::strftime(buf, sizeof(buf), format, &local) == 0) return "";
  return buf;
}

bool looksBinary(const std::string& bytes) {
  const size_t probe = std::min<size_t>(bytes.size(), 1024);
  for (size_t i = 0; i < probe; ++i) {
    if (bytes[i] == '\0') return true;
  }
  return false;
}

constexpr size_t kFileLimit = 200 * 1024;

}  // namespace

std::vector<Var> parse(const std::string& text) {
  std::vector<Var> out;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern()); it != std::sregex_iterator(); ++it) {
    const std::smatch& m = *it;
    Var v;
    v.raw = m[0];
    v.name = canonical(m[1]);
    v.arg = core::trim(m[2]);
    v.def = m[3].matched ? core::trim(m[3]) : "";
    v.pos = static_cast<size_t>(m.position(0));
    out.push_back(v);
  }
  return out;
}

std::vector<Var> inputs(const std::vector<Var>& vars) {
  std::vector<Var> out;
  for (const Var& v : vars) {
    if (v.name != "input") continue;
    const bool seen = std::any_of(out.begin(), out.end(), [&](const Var& o) { return o.arg == v.arg; });
    if (!seen) out.push_back(v);
  }
  return out;
}

const std::vector<std::string>& knownNames() {
  static const std::vector<std::string> names = {
      "clipboard", "selection", "date", "time", "datetime", "weekday", "timezone",
      "user", "model", "title", "file", "input",
  };
  return names;
}

bool hasVariables(const std::string& text) {
  if (text.find("{{") == std::string::npos) return false;
  const auto& known = knownNames();
  for (const Var& v : parse(text)) {
    if (std::find(known.begin(), known.end(), v.name) != known.end()) return true;
  }
  return false;
}

Expansion expand(const std::string& text, const Context& ctx) {
  Expansion out;
  const std::time_t now = ctx.now ? ctx.now : std::time(nullptr);
  size_t cursor = 0;
  for (const Var& v : parse(text)) {
    out.text.append(text, cursor, v.pos - cursor);
    cursor = v.pos + v.raw.size();
    std::string value;
    bool known = true;
    if (v.name == "clipboard") {
      value = ctx.clipboard ? ctx.clipboard() : "";
      out.notes.push_back(value.empty() ? "clipboard: empty" : "clipboard: " + sizeText(value.size()));
    } else if (v.name == "selection") {
      value = ctx.selection ? ctx.selection() : "";
      out.notes.push_back(value.empty() ? "selection: nothing selected" : "selection: " + sizeText(value.size()));
    } else if (v.name == "date") {
      value = timeText(now, "%Y-%m-%d");
    } else if (v.name == "time") {
      value = timeText(now, "%H:%M");
    } else if (v.name == "datetime") {
      value = timeText(now, "%Y-%m-%d %H:%M");
    } else if (v.name == "weekday") {
      value = timeText(now, "%A");
    } else if (v.name == "timezone") {
      value = timeText(now, "%Z");
    } else if (v.name == "user") {
      value = ctx.user;
    } else if (v.name == "model") {
      value = ctx.model;
    } else if (v.name == "title") {
      value = ctx.chatTitle;
    } else if (v.name == "file") {
      const std::string bytes = ctx.file && !v.arg.empty() ? ctx.file(v.arg) : "";
      if (bytes.empty()) {
        out.notes.push_back("file " + v.arg + ": not found or empty");
      } else if (looksBinary(bytes)) {
        value = "(binary file " + v.arg + ", " + sizeText(bytes.size()) + ")";
        out.notes.push_back("file " + v.arg + ": binary, skipped");
      } else if (bytes.size() > kFileLimit) {
        value = bytes.substr(0, kFileLimit) + "\n[... truncated at 200 KB ...]";
        out.notes.push_back("file " + v.arg + ": " + sizeText(bytes.size()) + ", truncated to 200 KB");
      } else {
        value = bytes;
        out.notes.push_back("file " + v.arg + ": " + sizeText(bytes.size()));
      }
    } else if (v.name == "input") {
      auto it = ctx.inputs.find(v.arg);
      value = it != ctx.inputs.end() ? it->second : v.def;
    } else {
      known = false;
    }
    out.text += known ? value : v.raw;
  }
  out.text.append(text, cursor, std::string::npos);
  return out;
}

}  // namespace promptvars
