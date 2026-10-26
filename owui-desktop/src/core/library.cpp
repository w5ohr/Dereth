#include "library.h"

#include <algorithm>
#include <set>

#include "common.h"

namespace library {
namespace {

std::string str(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

}  // namespace

std::vector<Entry> fromJson(const Json& j) {
  std::vector<Entry> out;
  if (!j.is_array()) return out;
  for (const auto& e : j) {
    if (!e.is_object()) continue;
    Entry entry;
    entry.id = str(e, "id");
    entry.chatId = str(e, "chatId");
    entry.chatTitle = str(e, "chatTitle");
    entry.messageId = str(e, "messageId");
    entry.role = str(e, "role");
    entry.model = str(e, "model");
    entry.text = str(e, "text");
    entry.note = str(e, "note");
    if (e.contains("tags") && e["tags"].is_array()) {
      for (const auto& t : e["tags"]) {
        if (t.is_string()) entry.tags.push_back(t.get<std::string>());
      }
      entry.tags = parseTags(core::join(entry.tags, ","));
    }
    if (e.contains("at") && e["at"].is_number_integer()) entry.at = e["at"].get<int64_t>();
    if (entry.text.empty()) continue;
    if (entry.id.empty()) entry.id = core::uuid();
    out.push_back(entry);
  }
  return out;
}

Json toJson(const std::vector<Entry>& entries) {
  Json out = Json::array();
  for (const Entry& e : entries) {
    out.push_back(Json{{"id", e.id}, {"chatId", e.chatId}, {"chatTitle", e.chatTitle}, {"messageId", e.messageId}, {"role", e.role}, {"model", e.model},
                       {"text", e.text}, {"note", e.note}, {"tags", e.tags}, {"at", e.at}});
  }
  return out;
}

std::string add(std::vector<Entry>& entries, Entry entry) {
  for (Entry& existing : entries) {
    if (!entry.messageId.empty() && existing.chatId == entry.chatId && existing.messageId == entry.messageId) {
      existing.text = entry.text;
      if (!entry.chatTitle.empty()) existing.chatTitle = entry.chatTitle;
      if (!entry.model.empty()) existing.model = entry.model;
      if (!entry.note.empty()) existing.note = entry.note;
      for (const std::string& t : entry.tags) {
        if (std::find(existing.tags.begin(), existing.tags.end(), t) == existing.tags.end()) existing.tags.push_back(t);
      }
      return existing.id;
    }
  }
  if (entry.id.empty()) entry.id = core::uuid();
  if (entry.at == 0) entry.at = core::nowSeconds();
  entry.tags = parseTags(core::join(entry.tags, ","));
  entries.push_back(entry);
  return entries.back().id;
}

bool remove(std::vector<Entry>& entries, const std::string& id) {
  const size_t before = entries.size();
  entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == id; }), entries.end());
  return entries.size() != before;
}

const Entry* find(const std::vector<Entry>& entries, const std::string& chatId, const std::string& messageId) {
  if (messageId.empty()) return nullptr;
  for (const Entry& e : entries) {
    if (e.chatId == chatId && e.messageId == messageId) return &e;
  }
  return nullptr;
}

std::vector<std::string> parseTags(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    std::string t = core::lower(core::trim(cur));
    while (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    cur.clear();
  };
  for (char c : text) {
    if (c == ',' || c == ' ' || c == '\n' || c == ';') flush();
    else cur += c;
  }
  flush();
  return out;
}

std::vector<Entry> search(const std::vector<Entry>& entries, const std::string& query) {
  std::vector<std::string> words;
  for (const std::string& w : core::split(core::lower(core::trim(query)), ' ')) {
    if (!w.empty()) words.push_back(w);
  }
  std::vector<Entry> out;
  for (const Entry& e : entries) {
    bool all = true;
    for (const std::string& w : words) {
      bool hit = false;
      if (w.size() > 1 && w[0] == '#') {
        const std::string tag = w.substr(1);
        for (const std::string& t : e.tags) hit = hit || t == tag || t.find(tag) == 0;
      } else {
        hit = core::lower(e.text).find(w) != std::string::npos || core::lower(e.note).find(w) != std::string::npos || core::lower(e.chatTitle).find(w) != std::string::npos ||
              core::lower(e.model).find(w) != std::string::npos;
        for (const std::string& t : e.tags) hit = hit || t.find(w) != std::string::npos;
      }
      if (!hit) {
        all = false;
        break;
      }
    }
    if (all) out.push_back(e);
  }
  std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.at > b.at; });
  return out;
}

std::vector<std::pair<std::string, int>> tagCounts(const std::vector<Entry>& entries) {
  std::map<std::string, int> counts;
  for (const Entry& e : entries) {
    for (const std::string& t : e.tags) counts[t]++;
  }
  std::vector<std::pair<std::string, int>> out(counts.begin(), counts.end());
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
  return out;
}

std::string snippet(const std::string& text, size_t maxChars) {
  std::string s;
  bool space = false;
  for (char c : text) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      if (!space && !s.empty()) s += ' ';
      space = true;
    } else {
      s += c;
      space = false;
    }
  }
  s = core::trim(s);
  if (s.size() <= maxChars) return s;
  size_t cut = maxChars;
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
  return s.substr(0, cut) + "...";
}

std::string markdown(const std::vector<Entry>& entries) {
  std::string out = "# Saved answers\n\n";
  for (const Entry& e : entries) {
    out += "## " + (e.chatTitle.empty() ? std::string("Untitled chat") : e.chatTitle) + " - " + core::formatTime(e.at, "%Y-%m-%d") + "\n\n";
    std::string meta;
    if (!e.model.empty()) meta += e.model;
    if (!e.tags.empty()) meta += (meta.empty() ? "" : " - ") + std::string("tags: ") + core::join(e.tags, ", ");
    if (!meta.empty()) out += "_" + meta + "_\n\n";
    if (!e.note.empty()) out += "> " + core::replaceAll(e.note, "\n", "\n> ") + "\n\n";
    out += e.text + "\n\n";
  }
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out + "\n";
}

}  // namespace library
