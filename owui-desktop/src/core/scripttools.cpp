#include "scripttools.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "common.h"

namespace scripttools {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

bool validName(const std::string& name) {
  if (name.empty() || name.size() > 40) return false;
  for (char c : name) {
    if (!(islower(static_cast<unsigned char>(c)) || isdigit(static_cast<unsigned char>(c)) || c == '_')) return false;
  }
  return true;
}

// Strips one comment leader: "# ", "// ", "-- ", "; ", "* ", "<!-- ".
std::string uncomment(const std::string& raw) {
  std::string line = core::trim(raw);
  for (const char* lead : {"#", "//", "--", ";", "*", "<!--", "REM ", "rem "}) {
    if (core::startsWith(line, lead)) {
      line = core::trim(line.substr(strlen(lead)));
      break;
    }
  }
  if (core::endsWith(line, "-->")) line = core::trim(line.substr(0, line.size() - 3));
  return line;
}

std::vector<std::string> runnerFor(const std::string& path, const std::string& firstLine) {
  const std::string ext = lower(core::extension(path));
  if (ext == "sh" || ext == "zsh") return {"/bin/zsh"};
  if (ext == "bash") return {"/bin/bash"};
  if (ext == "py") return {"python3"};
  if (ext == "applescript" || ext == "scpt") return {"osascript"};
  if (ext == "js" || ext == "jxa") return {"osascript", "-l", "JavaScript"};
  if (ext == "rb") return {"ruby"};
  if (ext == "pl") return {"perl"};
  if (ext == "swift") return {"swift"};
  if (ext == "php") return {"php"};
  if (core::startsWith(firstLine, "#!")) return {};   // the shebang decides; the file must be executable
  return {"/bin/zsh"};
}

bool isType(const std::string& t) {
  return t == "string" || t == "integer" || t == "number" || t == "boolean" || t == "array" || t == "object";
}

std::string valueText(const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_null()) return "";
  return v.dump();
}

}  // namespace

Json Tool::schema() const {
  Json properties = Json::object();
  Json required = Json::array();
  for (const Param& p : params) {
    Json prop{{"type", p.type}};
    if (!p.description.empty()) prop["description"] = p.description;
    if (!p.enumValues.empty()) prop["enum"] = p.enumValues;
    if (p.type == "array") prop["items"] = Json{{"type", "string"}};
    properties[p.name] = prop;
    if (p.required) required.push_back(p.name);
  }
  Json out{{"type", "object"}, {"properties", properties}};
  if (!required.empty()) out["required"] = required;
  return out;
}

Tool parse(const std::string& path, const std::string& content) {
  Tool tool;
  tool.path = path;
  tool.file = core::baseName(path);
  const std::vector<std::string> lines = core::split(content, '\n');
  bool found = false;
  std::string firstLine = lines.empty() ? "" : core::trim(lines[0]);
  for (size_t i = 0; i < lines.size() && i < 80; ++i) {
    const std::string line = uncomment(lines[i]);
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = lower(core::trim(line.substr(0, colon)));
    const std::string value = core::trim(line.substr(colon + 1));
    if (key == "owui-tool") {
      found = true;
      tool.name = lower(value);
      if (!validName(tool.name)) tool.error = "tool name must be lowercase letters, digits and underscores (got \"" + value + "\")";
    } else if (!found) {
      continue;   // nothing before the owui-tool line counts
    } else if (key == "description") {
      tool.description = value;
    } else if (key == "param") {
      // name type [required] [enum(a|b|c)] [- description]
      Param p;
      std::string spec = value, desc;
      const size_t dash = spec.find(" - ");
      if (dash != std::string::npos) {
        desc = core::trim(spec.substr(dash + 3));
        spec = spec.substr(0, dash);
      }
      std::vector<std::string> tokens;
      for (const std::string& t : core::split(spec, ' ')) {
        if (!core::trim(t).empty()) tokens.push_back(core::trim(t));
      }
      if (tokens.empty()) {
        if (tool.error.empty()) tool.error = "a param line has no name";
        continue;
      }
      p.name = tokens[0];
      p.description = desc;
      for (size_t t = 1; t < tokens.size(); ++t) {
        const std::string tok = lower(tokens[t]);
        if (isType(tok)) p.type = tok;
        else if (tok == "required") p.required = true;
        else if (tok == "optional") p.required = false;
        else if (core::startsWith(tok, "enum(") && tok.back() == ')') {
          for (const std::string& e : core::split(tokens[t].substr(5, tokens[t].size() - 6), '|')) {
            if (!core::trim(e).empty()) p.enumValues.push_back(core::trim(e));
          }
        } else if (p.description.empty() && desc.empty()) {
          // Loose form "param: name string the description words"
          std::string rest;
          for (size_t r = t; r < tokens.size(); ++r) rest += (rest.empty() ? "" : " ") + tokens[r];
          p.description = rest;
          break;
        }
      }
      if (!validName(lower(p.name)) && tool.error.empty()) tool.error = "param \"" + p.name + "\": use lowercase letters, digits and underscores";
      p.name = lower(p.name);
      tool.params.push_back(p);
    } else if (key == "timeout") {
      tool.timeoutSec = std::clamp(atoi(value.c_str()), 1, 3600);
    } else if (key == "sandbox") {
      const std::string v = lower(value);
      tool.sandbox = !(v == "off" || v == "no" || v == "false" || v == "0");
    } else if (key == "runner") {
      tool.runner.clear();
      for (const std::string& t : core::split(value, ' ')) {
        if (!core::trim(t).empty()) tool.runner.push_back(core::trim(t));
      }
    }
  }
  if (!found) {
    tool.error = "no owui-tool header";
    return tool;
  }
  if (tool.runner.empty()) tool.runner = runnerFor(path, firstLine);
  if (tool.runner.empty()) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !(st.st_mode & S_IXUSR)) {
      if (tool.error.empty()) tool.error = "shebang script is not executable (chmod +x " + tool.file + ")";
    }
  }
  if (tool.description.empty()) tool.description = "Runs " + tool.file;
  return tool;
}

std::vector<Tool> scan(const std::string& dir) {
  std::vector<Tool> out;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return out;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    if (entry.file_size(ec) > 512 * 1024) continue;
    bool ok = false;
    const std::string content = core::readFile(entry.path().string(), &ok);
    if (!ok || content.find("owui-tool") == std::string::npos) continue;
    out.push_back(parse(entry.path().string(), content));
  }
  std::sort(out.begin(), out.end(), [](const Tool& a, const Tool& b) { return a.name < b.name; });
  // Two files claiming one name: the later one is an error, not a silent override.
  for (size_t i = 1; i < out.size(); ++i) {
    if (out[i].name == out[i - 1].name && out[i].error.empty()) out[i].error = "duplicate tool name (also in " + out[i - 1].file + ")";
  }
  return out;
}

std::string validateArgs(const Tool& tool, const Json& args) {
  if (!args.is_null() && !args.is_object()) return "arguments must be a JSON object";
  for (const Param& p : tool.params) {
    const bool present = args.is_object() && args.contains(p.name) && !args[p.name].is_null();
    if (p.required && !present) return "missing required argument \"" + p.name + "\"";
    if (!present) continue;
    const Json& v = args[p.name];
    if (!p.enumValues.empty()) {
      const std::string s = valueText(v);
      if (std::find(p.enumValues.begin(), p.enumValues.end(), s) == p.enumValues.end()) {
        return "\"" + p.name + "\" must be one of " + core::join(p.enumValues, ", ");
      }
    }
  }
  return "";
}

std::vector<std::string> argv(const Tool& tool, const Json& args) {
  const Json a = args.is_object() ? args : Json::object();
  std::vector<std::string> out = {"/usr/bin/env", "OWUI_ARGS=" + a.dump(), "OWUI_TOOL=" + tool.name};
  for (const auto& [key, value] : a.items()) {
    std::string env;
    for (char c : upper(key)) env += (isalnum(static_cast<unsigned char>(c)) ? c : '_');
    out.push_back("OWUI_ARG_" + env + "=" + valueText(value));
  }
  for (const std::string& r : tool.runner) out.push_back(r);
  out.push_back(tool.path);
  out.push_back(a.dump());
  return out;
}

std::vector<std::pair<std::string, std::string>> examples() {
  return {
      {"disk_usage.sh",
       "#!/bin/zsh\n"
       "# owui-tool: disk_usage\n"
       "# description: Disk usage of a folder, biggest entries first (du -h).\n"
       "# param: path string required - Absolute folder path\n"
       "# param: depth integer - How many levels deep (default 1)\n"
       "# timeout: 60\n"
       "# sandbox: on\n"
       "du -h -d \"${OWUI_ARG_DEPTH:-1}\" \"$OWUI_ARG_PATH\" 2>/dev/null | sort -hr | head -40\n"},
      {"word_count.py",
       "#!/usr/bin/env python3\n"
       "# owui-tool: word_count\n"
       "# description: Count the words, lines and characters in a text.\n"
       "# param: text string required - The text to measure\n"
       "import json, os\n"
       "args = json.loads(os.environ.get(\"OWUI_ARGS\", \"{}\"))\n"
       "t = args.get(\"text\", \"\")\n"
       "print(json.dumps({\"words\": len(t.split()), \"lines\": t.count(\"\\n\") + 1, \"chars\": len(t)}))\n"},
      {"frontmost_app.js",
       "// owui-tool: frontmost_app\n"
       "// description: Name of the application in front right now (JXA).\n"
       "// sandbox: off\n"
       "function run(argv) {\n"
       "  const se = Application('System Events');\n"
       "  const procs = se.applicationProcesses.whose({frontmost: true})();\n"
       "  return procs.length ? procs[0].name() : 'unknown';\n"
       "}\n"},
  };
}

}  // namespace scripttools
