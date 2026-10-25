#include "vault.h"

#include <algorithm>
#include <set>

#include "common.h"

namespace vault {
namespace {

std::string noteBody(const Json& note) {
  const Json data = note.value("data", Json::object());
  const Json content = data.value("content", Json());
  if (content.is_string()) return content.get<std::string>();
  if (content.is_object() && content.contains("md") && content["md"].is_string()) return content["md"];
  return "";
}

// value() throws when a key is present but null (parentId of a root
// message is exactly that), so strings are read through this.
std::string str(const Json& j, const char* key, const std::string& fallback = "") {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return fallback;
  return j[key].get<std::string>();
}

// Chats carry unix seconds; notes carry nanoseconds (Open WebUI's time_ns).
std::string isoTime(int64_t stamp) {
  if (stamp <= 0) return "";
  if (stamp > 100000000000000LL) stamp /= 1000000000;   // ns
  else if (stamp > 100000000000LL) stamp /= 1000;      // ms
  return core::formatTime(stamp, "%Y-%m-%dT%H:%M:%S");
}

}  // namespace

std::string safeName(const std::string& title) {
  std::string out;
  for (unsigned char c : title) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '#' ||
        c == '^' || c == '[' || c == ']' || c < 0x20) {
      out += ' ';
    } else {
      out += static_cast<char>(c);
    }
  }
  // Collapse runs of spaces, trim, drop leading dots.
  std::string collapsed;
  for (char c : out) {
    if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
    collapsed += c;
  }
  collapsed = core::trim(collapsed);
  while (!collapsed.empty() && collapsed.front() == '.') collapsed.erase(0, 1);
  if (collapsed.size() > 80) {
    collapsed = collapsed.substr(0, 80);
    // Do not cut a UTF-8 sequence in half.
    while (!collapsed.empty() && (static_cast<unsigned char>(collapsed.back()) & 0xC0) == 0x80) collapsed.pop_back();
    collapsed = core::trim(collapsed);
  }
  return collapsed.empty() ? "Untitled" : collapsed;
}

std::string yamlQuote(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') {
      out += "\\n";
      continue;
    }
    out += c;
  }
  return out + "\"";
}

std::string frontMatter(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out = "---\n";
  for (const auto& [key, value] : fields) out += key + ": " + value + "\n";
  return out + "---\n";
}

std::map<std::string, std::string> readFrontMatter(const std::string& text, std::string* body) {
  std::map<std::string, std::string> out;
  if (body) *body = text;
  if (!core::startsWith(text, "---\n") && !core::startsWith(text, "---\r\n")) return out;
  const size_t start = text.find('\n') + 1;
  size_t end = text.find("\n---", start);
  if (end == std::string::npos) return out;
  const std::string block = text.substr(start, end - start);
  for (const std::string& raw : core::split(block, '\n')) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t colon = line.find(':');
    if (colon == std::string::npos || line.empty() || line[0] == ' ') continue;
    std::string value = core::trim(line.substr(colon + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      std::string unq;
      for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) {
          ++i;
          unq += value[i] == 'n' ? '\n' : value[i];
        } else {
          unq += value[i];
        }
      }
      value = unq;
    }
    out[core::trim(line.substr(0, colon))] = value;
  }
  if (body) {
    size_t after = end + 4;   // past "\n---"
    if (after < text.size() && text[after] == '\r') ++after;
    if (after < text.size() && text[after] == '\n') ++after;
    *body = text.substr(std::min(after, text.size()));
  }
  return out;
}

std::string chatFileName(const Json& chat) {
  const std::string id = str(chat, "id");
  return safeName(str(chat, "title")) + " (" + id.substr(0, 8) + ").md";
}

std::string chatMarkdown(const Json& response, const std::string& serverUrl) {
  const Json chat = response.value("chat", Json::object());
  const std::string id = str(response, "id");
  const std::string title = str(response, "title", str(chat, "title"));
  std::string models, tags;
  for (const auto& m : chat.value("models", Json::array())) {
    if (m.is_string()) models += (models.empty() ? "" : ", ") + m.get<std::string>();
  }
  Json tagList = chat.value("tags", Json::array());
  if (tagList.empty() && chat.contains("meta") && chat["meta"].is_object()) tagList = chat["meta"].value("tags", Json::array());
  for (const auto& t : tagList) {
    if (t.is_string()) tags += (tags.empty() ? "" : ", ") + t.get<std::string>();
  }
  std::vector<std::pair<std::string, std::string>> fields = {
      {"id", id},
      {"title", yamlQuote(title)},
      {"created", isoTime(response.value("created_at", static_cast<int64_t>(0)))},
      {"updated", isoTime(response.value("updated_at", static_cast<int64_t>(0)))},
      {"models", "[" + models + "]"},
      {"tags", "[" + tags + "]"},
      {"type", "chat"},
  };
  if (!serverUrl.empty()) fields.push_back({"source", serverUrl + "/c/" + id});
  std::string out = frontMatter(fields) + "\n# " + title + "\n\n";

  // The current branch: walk up from currentId, then write forward.
  const Json history = chat.value("history", Json::object());
  const Json messages = history.value("messages", Json::object());
  std::vector<std::string> path;
  std::set<std::string> seen;
  std::string cursor = str(history, "currentId");
  while (!cursor.empty() && messages.contains(cursor) && !seen.count(cursor)) {
    seen.insert(cursor);
    path.push_back(cursor);
    cursor = str(messages[cursor], "parentId");
  }
  std::reverse(path.begin(), path.end());
  if (path.empty() && chat.contains("messages") && chat["messages"].is_array()) {
    for (const auto& m : chat["messages"]) {
      if (m.is_object() && m.contains("id") && m["id"].is_string()) path.push_back(m["id"]);
    }
  }
  for (const std::string& mid : path) {
    const Json& m = messages.contains(mid) ? messages[mid] : Json::object();
    if (!m.is_object()) continue;
    const std::string role = str(m, "role");
    std::string who = role == "user" ? "You" : str(m, "modelName", str(m, "model", "Assistant"));
    if (who.empty()) who = "Assistant";
    out += "## " + who;
    const int64_t ts = m.contains("timestamp") && m["timestamp"].is_number() ? m["timestamp"].get<int64_t>() : 0;
    if (ts > 0) out += "  _" + core::formatTime(ts) + "_";
    const Json& content = m.contains("content") ? m["content"] : Json("");
    out += "\n\n" + (content.is_string() ? content.get<std::string>() : content.dump()) + "\n\n";
  }
  return out;
}

std::string noteFileName(const Json& note) { return safeName(str(note, "title")) + ".md"; }

std::string noteMarkdown(const Json& note) {
  std::vector<std::pair<std::string, std::string>> fields = {
      {"id", str(note, "id")},
      {"title", yamlQuote(str(note, "title"))},
      {"updated", isoTime(note.value("updated_at", static_cast<int64_t>(0)))},
      {"type", "note"},
  };
  std::string body = noteBody(note);
  if (!body.empty() && body.back() != '\n') body += "\n";
  return frontMatter(fields) + body;
}

NoteFile parseNoteFile(const std::string& text, const std::string& fileName) {
  NoteFile out;
  std::string body;
  const std::map<std::string, std::string> fm = readFrontMatter(text, &body);
  out.ok = true;
  auto id = fm.find("id");
  auto title = fm.find("title");
  if (id != fm.end()) out.id = id->second;
  if (title != fm.end() && !title->second.empty()) {
    out.title = title->second;
  } else {
    std::string stem = fileName;
    const size_t slash = stem.rfind('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    if (core::endsWith(stem, ".md")) stem = stem.substr(0, stem.size() - 3);
    out.title = stem.empty() ? "Untitled" : stem;
  }
  out.body = body;
  return out;
}

}  // namespace vault
