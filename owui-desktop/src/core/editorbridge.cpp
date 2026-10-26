#include "editorbridge.h"

#include <cctype>
#include <cstdlib>

#include "common.h"

namespace editorbridge {
namespace {

bool allDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  size_t at = 0;
  while ((at = s.find(from, at)) != std::string::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
  return s;
}

}  // namespace

bool parseFileRef(const std::string& tokenIn, FileRef* out) {
  std::string token = core::trim(tokenIn);
  if (token.size() < 4 || token.find("://") != std::string::npos || token.find(' ') != std::string::npos) return false;
  // Peel up to two trailing ":<digits>" groups: line, then column.
  int numbers[2] = {0, 0};
  int count = 0;
  while (count < 2) {
    const size_t colon = token.rfind(':');
    if (colon == std::string::npos || colon + 1 >= token.size()) break;
    const std::string tail = token.substr(colon + 1);
    if (!allDigits(tail) || tail.size() > 7) break;
    numbers[count++] = atoi(tail.c_str());
    token = token.substr(0, colon);
  }
  if (count == 0) return false;
  const std::string path = token;
  if (path.empty() || allDigits(path) || path.find(':') != std::string::npos) return false;
  // A path has a directory part or an extension; "12" or "abc" do not.
  const size_t slash = path.rfind('/');
  const size_t dot = path.rfind('.');
  const bool hasExt = dot != std::string::npos && dot + 1 < path.size() && (slash == std::string::npos || dot > slash) && dot > 0;
  if (slash == std::string::npos && !hasExt) return false;
  if (path.back() == '/' || path.back() == '.') return false;
  out->path = path;
  if (count == 1) {
    out->line = numbers[0];
    out->column = 0;
  } else {
    out->line = numbers[1];   // peeled right to left: the last number was the column
    out->column = numbers[0];
  }
  return out->line >= 1;
}

std::string linkFor(const FileRef& ref) {
  std::string out = "editor://" + ref.path + ":" + std::to_string(ref.line);
  if (ref.column > 0) out += ":" + std::to_string(ref.column);
  return out;
}

bool parseLink(const std::string& link, FileRef* out) {
  if (!core::startsWith(link, "editor://")) return false;
  return parseFileRef(link.substr(9), out);
}

std::string resolvePath(const std::string& path, const std::string& projectRoot, const std::string& home) {
  if (core::startsWith(path, "~/") && !home.empty()) return home + path.substr(1);
  if (!path.empty() && path[0] == '/') return path;
  if (!projectRoot.empty()) return projectRoot + (projectRoot.back() == '/' ? "" : "/") + path;
  return path;
}

Editor editorFrom(const std::string& id) {
  const std::string k = core::lower(core::trim(id));
  if (k == "cursor") return Editor::Cursor;
  if (k == "zed") return Editor::Zed;
  if (k == "xcode") return Editor::Xcode;
  if (k == "sublime") return Editor::Sublime;
  if (k == "custom") return Editor::Custom;
  return Editor::VSCode;
}

std::string editorId(Editor e) {
  switch (e) {
    case Editor::Cursor: return "cursor";
    case Editor::Zed: return "zed";
    case Editor::Xcode: return "xcode";
    case Editor::Sublime: return "sublime";
    case Editor::Custom: return "custom";
    default: return "vscode";
  }
}

std::string editorName(Editor e) {
  switch (e) {
    case Editor::Cursor: return "Cursor";
    case Editor::Zed: return "Zed";
    case Editor::Xcode: return "Xcode";
    case Editor::Sublime: return "Sublime Text";
    case Editor::Custom: return "custom command";
    default: return "VS Code";
  }
}

std::vector<std::string> splitArgs(const std::string& command) {
  std::vector<std::string> out;
  std::string current;
  bool inQuote = false;
  char quote = 0;
  bool any = false;
  for (char c : command) {
    if (inQuote) {
      if (c == quote) inQuote = false;
      else current += c;
      continue;
    }
    if (c == '"' || c == '\'') {
      inQuote = true;
      quote = c;
      any = true;
      continue;
    }
    if (isspace(static_cast<unsigned char>(c))) {
      if (!current.empty() || any) out.push_back(current);
      current.clear();
      any = false;
      continue;
    }
    current += c;
    any = true;
  }
  if (!current.empty() || any) out.push_back(current);
  return out;
}

std::vector<std::string> openCommand(Editor e, const FileRef& ref, const std::string& customTemplate) {
  const std::string line = std::to_string(ref.line);
  const std::string col = std::to_string(ref.column > 0 ? ref.column : 1);
  switch (e) {
    case Editor::VSCode: return {"/usr/bin/open", "vscode://file" + ref.path + ":" + line + ":" + col};
    case Editor::Cursor: return {"/usr/bin/open", "cursor://file" + ref.path + ":" + line + ":" + col};
    case Editor::Zed: return {"/usr/bin/open", "zed://file" + ref.path + ":" + line + ":" + col};
    case Editor::Xcode: return {"/usr/bin/xed", "-l", line, ref.path};
    case Editor::Sublime: return {"subl", ref.path + ":" + line + ":" + col};
    case Editor::Custom: {
      std::string cmd = customTemplate;
      cmd = replaceAll(cmd, "{file}", "\"" + ref.path + "\"");
      cmd = replaceAll(cmd, "{line}", line);
      cmd = replaceAll(cmd, "{col}", col);
      return splitArgs(cmd);
    }
  }
  return {};
}

std::string explainPrompt(const std::string& file, int line, const std::string& language, const std::string& text) {
  std::string where = file;
  if (line > 0) where += ":" + std::to_string(line);
  std::string out = "Explain this code";
  if (!where.empty()) out += " from " + where;
  out += ". What does it do, what could go wrong, and how would you improve it?\n\n```" + language + "\n" + text + "\n```";
  return out;
}

}  // namespace editorbridge
