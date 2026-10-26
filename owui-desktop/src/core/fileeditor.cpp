#include "fileeditor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common.h"

namespace fileeditor {
namespace {

using Json = nlohmann::json;

std::string hashHex(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[20];
  snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == '\n') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

}  // namespace

std::string historyFile(const std::string& historyDir, const std::string& path) { return historyDir + "/" + hashHex(path) + ".json"; }

std::vector<Snapshot> loadHistory(const std::string& historyDir, const std::string& path) {
  std::vector<Snapshot> out;
  bool ok = false;
  const std::string raw = core::readFile(historyFile(historyDir, path), &ok);
  if (!ok) return out;
  const Json j = Json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("snapshots") || !j["snapshots"].is_array()) return out;
  for (const auto& s : j["snapshots"]) {
    if (!s.is_object()) continue;
    out.push_back(Snapshot{s.value("at", static_cast<int64_t>(0)), s.value("label", ""), s.value("content", "")});
  }
  return out;
}

bool record(const std::string& historyDir, const std::string& path, const std::string& content, const std::string& label, size_t keep) {
  std::vector<Snapshot> history = loadHistory(historyDir, path);
  if (!history.empty() && history.back().content == content) return true;
  history.push_back(Snapshot{core::nowSeconds(), label, content});
  while (history.size() > keep) history.erase(history.begin());
  Json j{{"path", path}, {"snapshots", Json::array()}};
  for (const Snapshot& s : history) j["snapshots"].push_back(Json{{"at", s.at}, {"label", s.label}, {"content", s.content}});
  std::error_code ec;
  std::filesystem::create_directories(historyDir, ec);
  return core::writeFile(historyFile(historyDir, path), j.dump());
}

std::string describe(const Snapshot& s, int64_t now) {
  const int lines = lineCount(s.content);
  std::string when;
  const int64_t ago = std::max<int64_t>(0, now - s.at);
  if (ago < 60) when = "just now";
  else if (ago < 3600) when = std::to_string(ago / 60) + " min ago";
  else if (ago < 86400) when = std::to_string(ago / 3600) + " h ago";
  else when = std::to_string(ago / 86400) + " d ago";
  return s.label + " - " + std::to_string(lines) + (lines == 1 ? " line" : " lines") + " - " + when;
}

std::string languageFor(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  const size_t slash = path.find_last_of('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    const std::string base = lower(slash == std::string::npos ? path : path.substr(slash + 1));
    if (base == "makefile" || base == "dockerfile") return "bash";
    return "";
  }
  const std::string ext = lower(path.substr(dot + 1));
  if (ext == "py" || ext == "pyw") return "python";
  if (ext == "js" || ext == "mjs" || ext == "cjs" || ext == "jsx") return "javascript";
  if (ext == "ts" || ext == "tsx") return "typescript";
  if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "hpp" || ext == "hh" || ext == "h" || ext == "c" || ext == "mm" || ext == "m") return "cpp";
  if (ext == "json") return "json";
  if (ext == "md" || ext == "markdown") return "markdown";
  if (ext == "sh" || ext == "bash" || ext == "zsh") return "bash";
  if (ext == "html" || ext == "htm" || ext == "xml" || ext == "svg") return "html";
  if (ext == "css" || ext == "scss") return "css";
  if (ext == "rs") return "rust";
  if (ext == "go") return "go";
  if (ext == "java" || ext == "kt") return "java";
  if (ext == "swift") return "swift";
  if (ext == "yaml" || ext == "yml") return "yaml";
  if (ext == "toml" || ext == "ini" || ext == "cfg") return "toml";
  if (ext == "sql") return "sql";
  if (ext == "rb") return "ruby";
  if (ext == "cmake" || ext == "txt") return "";
  return "";
}

int lineCount(const std::string& text) {
  if (text.empty()) return 0;
  int n = 1;
  for (char c : text) {
    if (c == '\n') ++n;
  }
  if (text.back() == '\n') --n;
  return n;
}

int lineOf(const std::string& text, size_t offset) {
  int line = 1;
  const size_t end = std::min(offset, text.size());
  for (size_t i = 0; i < end; ++i) {
    if (text[i] == '\n') ++line;
  }
  return line;
}

size_t offsetOfLine(const std::string& text, int line) {
  if (line <= 1) return 0;
  int current = 1;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      if (++current == line) return i + 1;
    }
  }
  return text.size();
}

size_t findNext(const std::string& text, const std::string& needle, size_t from, bool caseless) {
  if (needle.empty() || text.empty()) return std::string::npos;
  const std::string hay = caseless ? lower(text) : text;
  const std::string n = caseless ? lower(needle) : needle;
  size_t pos = hay.find(n, std::min(from, hay.size()));
  if (pos == std::string::npos && from > 0) pos = hay.find(n, 0);
  return pos;
}

int firstDifferingLine(const std::string& a, const std::string& b) {
  if (a == b) return 0;
  const std::vector<std::string> la = splitLines(a), lb = splitLines(b);
  const size_t n = std::min(la.size(), lb.size());
  for (size_t i = 0; i < n; ++i) {
    if (la[i] != lb[i]) return static_cast<int>(i) + 1;
  }
  return static_cast<int>(n) + 1;
}

}  // namespace fileeditor
