#include "fileblocks.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "common.h"

namespace fileblocks {
namespace {

std::string stripQuotes(std::string s) {
  s = core::trim(s);
  // Quotes and trailing prose punctuation ("`src/a.py`:" / "a.py,") come off
  // in any order until nothing changes.
  for (;;) {
    const std::string was = s;
    while (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'') ||
                             (s.front() == '`' && s.back() == '`') || (s.front() == '*' && s.back() == '*') ||
                             (s.front() == '_' && s.back() == '_'))) {
      s = s.substr(1, s.size() - 2);
    }
    while (!s.empty() && (s.back() == ':' || s.back() == ',' || s.back() == '.' || s.back() == ';')) s.pop_back();
    if (s == was) return s;
  }
}

// Path from an info string: "python path=a.py", "ts title=\"x.ts\"", "js src/app.js".
std::string pathFromInfo(const std::string& info, std::string* language) {
  std::string path;
  bool first = true;
  for (const std::string& rawToken : core::split(info, ' ')) {
    const std::string token = core::trim(rawToken);
    if (token.empty()) continue;
    if (first) {
      first = false;
      if (!looksLikePath(token) || token.find('.') == std::string::npos) {
        *language = core::lower(token);
        continue;
      }
    }
    for (const char* key : {"path=", "title=", "file=", "filename=", "name="}) {
      if (core::startsWith(core::lower(token), key)) {
        path = stripQuotes(token.substr(std::string(key).size()));
        break;
      }
    }
    if (!path.empty()) break;
    if (looksLikePath(token)) path = stripQuotes(token);
  }
  return path;
}

// "// file: a.py", "# path: a.py", "<!-- a.html -->", "/* a.css */", "-- a.sql".
std::string pathFromComment(const std::string& line) {
  static const std::regex re(
      R"(^\s*(?://|#|--|;|/\*|<!--|%)\s*(?:(?:file(?:name)?|path)\s*[:=]\s*)?([^\s*>]+)\s*(?:\*/|-->)?\s*$)",
      std::regex::icase);
  std::smatch m;
  if (!std::regex_match(line, m, re)) return "";
  const std::string candidate = stripQuotes(m[1]);
  // A bare comment line that merely contains a dotted word ("# TODO.") is not a path:
  // require an extension-looking tail or a slash, and no spaces (regex ensured).
  return looksLikePath(candidate) ? candidate : "";
}

// "### src/a.py", "**a.py**", "`src/a.py`", "File: a.py", "src/a.py:".
std::string pathFromHeading(const std::string& rawLine) {
  std::string line = core::trim(rawLine);
  while (!line.empty() && line.front() == '#') line.erase(0, 1);
  line = core::trim(line);
  for (const char* prefix : {"file:", "file ", "path:", "filename:", "create ", "update ", "edit ", "in "}) {
    if (core::startsWith(core::lower(line), prefix)) {
      line = core::trim(line.substr(std::string(prefix).size()));
      break;
    }
  }
  // Take the first path-looking token; the line must be short (a label, not prose).
  if (line.size() > 120) return "";
  for (const std::string& token : core::split(line, ' ')) {
    const std::string candidate = stripQuotes(token);
    if (looksLikePath(candidate)) return candidate;
  }
  return "";
}

}  // namespace

bool looksLikePath(const std::string& tokenIn) {
  const std::string token = stripQuotes(tokenIn);
  if (token.empty() || token.size() > 240) return false;
  if (token.find(' ') != std::string::npos || token.find("://") != std::string::npos) return false;
  if (token.find('(') != std::string::npos || token.find(')') != std::string::npos) return false;
  const size_t slash = token.find('/');
  const size_t dot = token.rfind('.');
  const bool hasExt = dot != std::string::npos && dot + 1 < token.size() && dot + 8 >= token.size() &&
                      std::all_of(token.begin() + static_cast<long>(dot) + 1, token.end(),
                                  [](char c) { return std::isalnum(static_cast<unsigned char>(c)); }) &&
                      dot > 0;
  const bool hasDir = slash != std::string::npos && slash + 1 < token.size();
  // Files that go by name alone.
  static const char* kBare[] = {"Makefile", "Dockerfile", "Rakefile", "Gemfile", "Procfile", "Justfile",
                                "LICENSE", "README", "CODEOWNERS", "Vagrantfile", "Brewfile"};
  const std::string base = slash == std::string::npos ? token : token.substr(token.find_last_of('/') + 1);
  bool bare = false;
  for (const char* name : kBare) bare = bare || base == name;
  if (!hasExt && !hasDir && !bare) return false;
  // Version numbers and decimals ("3.11", "0.5") are not files.
  if (!hasDir && std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; })) return false;
  return true;
}

std::vector<Block> extract(const std::string& markdown) {
  std::vector<Block> out;
  const std::vector<std::string> lines = core::split(markdown, '\n');
  size_t i = 0;
  while (i < lines.size()) {
    const std::string trimmed = core::trim(lines[i]);
    if (!core::startsWith(trimmed, "```") && !core::startsWith(trimmed, "~~~")) {
      ++i;
      continue;
    }
    const std::string fence = trimmed.substr(0, 3);
    const std::string info = core::trim(trimmed.substr(3));
    // Body until the closing fence.
    size_t j = i + 1;
    std::vector<std::string> body;
    while (j < lines.size() && !core::startsWith(core::trim(lines[j]), fence)) body.push_back(lines[j++]);
    const bool closed = j < lines.size();

    Block block;
    block.path = pathFromInfo(info, &block.language);
    if (!block.path.empty()) block.source = "info";
    if (block.path.empty() && !body.empty()) {
      block.path = pathFromComment(body.front());
      if (!block.path.empty()) {
        block.source = "comment";
        body.erase(body.begin());
        if (!body.empty() && core::trim(body.front()).empty()) body.erase(body.begin());
      }
    }
    if (block.path.empty()) {
      // Nearest non-empty line above the fence.
      for (size_t k = i; k-- > 0;) {
        const std::string above = core::trim(lines[k]);
        if (above.empty()) continue;
        block.path = pathFromHeading(above);
        if (!block.path.empty()) block.source = "heading";
        break;
      }
    }
    if (!block.path.empty() && closed && block.language != "diff" && block.language != "patch") {
      std::string code;
      for (size_t k = 0; k < body.size(); ++k) code += body[k] + "\n";
      block.code = code;
      // A later block for the same path replaces the earlier one.
      auto same = std::find_if(out.begin(), out.end(), [&](const Block& b) { return b.path == block.path; });
      if (same != out.end()) *same = block;
      else out.push_back(block);
    }
    i = closed ? j + 1 : j;
  }
  return out;
}

}  // namespace fileblocks
