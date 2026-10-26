#include "retrieval.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#include "common.h"

namespace retrieval {
namespace {

std::string str(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

const Json& at(const Json& arr, size_t i) {
  static const Json kNull;
  if (!arr.is_array() || i >= arr.size()) return kNull;
  return arr[i];
}

}  // namespace

std::vector<Chunk> fromSources(const Json& sources) {
  std::vector<Chunk> out;
  if (!sources.is_array()) return out;
  for (size_t s = 0; s < sources.size(); ++s) {
    const Json& entry = sources[s];
    if (!entry.is_object()) continue;
    const Json src = entry.value("source", Json::object());
    const Json docs = entry.value("document", Json::array());
    const Json metas = entry.value("metadata", Json::array());
    const Json dists = entry.value("distances", Json::array());
    const size_t count = docs.is_array() ? docs.size() : 0;
    for (size_t i = 0; i < count; ++i) {
      if (!docs[i].is_string()) continue;
      Chunk c;
      c.source = static_cast<int>(s);
      c.index = static_cast<int>(i);
      c.text = core::trim(docs[i].get<std::string>());
      const Json& meta = at(metas, i);
      c.name = str(src, "name");
      if (c.name.empty()) c.name = str(meta, "name");
      if (c.name.empty()) c.name = str(meta, "source");
      c.url = str(src, "url");
      if (c.url.empty() && core::startsWith(str(meta, "source"), "http")) c.url = str(meta, "source");
      if (c.name.empty()) c.name = c.url.empty() ? "Source " + std::to_string(s + 1) : c.url;
      c.fileId = str(meta, "file_id");
      if (c.fileId.empty() && str(src, "type") == "file") c.fileId = str(src, "id");
      if (meta.is_object() && meta.contains("page") && meta["page"].is_number_integer()) c.page = meta["page"].get<int>() + 1;
      const Json& d = at(dists, i);
      if (d.is_number()) c.relevance = std::clamp(d.get<double>(), 0.0, 1.0);
      c.id = std::to_string(std::hash<std::string>{}(c.name + "#" + std::to_string(c.page) + "#" + c.text.substr(0, 200)));
      out.push_back(c);
    }
  }
  return out;
}

std::vector<Chunk> ranked(std::vector<Chunk> chunks) {
  std::stable_sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) {
    if ((a.relevance < 0) != (b.relevance < 0)) return a.relevance >= 0;
    return a.relevance > b.relevance;
  });
  return chunks;
}

Marks marksFromJson(const Json& j) {
  Marks m;
  if (!j.is_object()) return m;
  for (const char* key : {"pinned", "excluded"}) {
    const Json list = j.value(key, Json::array());
    if (!list.is_array()) continue;
    for (const auto& id : list) {
      if (id.is_string()) (std::string(key) == "pinned" ? m.pinned : m.excluded).insert(id.get<std::string>());
    }
  }
  return m;
}

Json marksToJson(const Marks& m) {
  return Json{{"pinned", Json(std::vector<std::string>(m.pinned.begin(), m.pinned.end()))},
              {"excluded", Json(std::vector<std::string>(m.excluded.begin(), m.excluded.end()))}};
}

void togglePin(Marks& m, const std::string& id) {
  if (m.pinned.count(id)) {
    m.pinned.erase(id);
    return;
  }
  m.pinned.insert(id);
  m.excluded.erase(id);
}

void toggleExclude(Marks& m, const std::string& id) {
  if (m.excluded.count(id)) {
    m.excluded.erase(id);
    return;
  }
  m.excluded.insert(id);
  m.pinned.erase(id);
}

Stats stats(const std::vector<Chunk>& chunks, const Marks& marks) {
  Stats s;
  std::set<int> sources;
  double sum = 0.0;
  for (const Chunk& c : chunks) {
    ++s.chunks;
    sources.insert(c.source);
    if (c.relevance >= 0) {
      ++s.scored;
      sum += c.relevance;
      s.best = std::max(s.best, c.relevance);
    }
    if (marks.pinned.count(c.id)) ++s.pinned;
    if (marks.excluded.count(c.id)) ++s.excluded;
  }
  s.sources = static_cast<int>(sources.size());
  if (s.scored > 0) s.mean = sum / s.scored;
  return s;
}

std::vector<Chunk> passagesForReask(const std::vector<Chunk>& chunks, const Marks& marks) {
  std::vector<Chunk> out;
  if (marks.pinned.empty() && marks.excluded.empty()) return out;
  for (const Chunk& c : chunks) {
    if (!marks.pinned.empty() ? marks.pinned.count(c.id) > 0 : marks.excluded.count(c.id) == 0) out.push_back(c);
  }
  return out;
}

std::string reaskPrompt(const std::string& question, const std::vector<Chunk>& passages) {
  std::string out = "Answer the question below using only the passages quoted here. Cite them as [1], [2], ... and say when they do not cover the answer.\n\n";
  int n = 1;
  for (const Chunk& c : passages) {
    out += "[" + std::to_string(n++) + "] " + c.name;
    if (c.page > 0) out += ", page " + std::to_string(c.page);
    out += ":\n\"\"\"\n" + c.text + "\n\"\"\"\n\n";
  }
  out += "Question: " + core::trim(question);
  return out;
}

std::string copyText(const std::vector<Chunk>& passages) {
  std::string out;
  int n = 1;
  for (const Chunk& c : passages) {
    out += std::to_string(n++) + ". " + c.name;
    if (c.page > 0) out += " (p. " + std::to_string(c.page) + ")";
    if (c.relevance >= 0) out += " - relevance " + relevanceLabel(c.relevance);
    out += "\n" + c.text + "\n\n";
  }
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

std::string relevanceLabel(double relevance) {
  if (relevance < 0) return "n/a";
  return std::to_string(static_cast<int>(std::lround(relevance * 100.0))) + "%";
}

}  // namespace retrieval
