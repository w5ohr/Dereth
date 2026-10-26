#include "modellib.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "common.h"

namespace modellib {
namespace {

std::string encode(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else if (c == ' ') out += '+';
    else {
      char buf[4];
      snprintf(buf, sizeof buf, "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string decodeEntities(std::string s) {
  const std::pair<const char*, const char*> table[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#x27;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
  for (const auto& [from, to] : table) {
    size_t at;
    while ((at = s.find(from)) != std::string::npos) s.replace(at, strlen(from), to);
  }
  return s;
}

std::string attrOf(const std::string& tag, const std::string& name) {
  const size_t at = tag.find(name + "=\"");
  if (at == std::string::npos) return "";
  const size_t start = at + name.size() + 2;
  const size_t end = tag.find('"', start);
  return end == std::string::npos ? "" : tag.substr(start, end - start);
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::string hfSearchUrl(const std::string& query) {
  return "https://huggingface.co/api/models?search=" + encode(core::trim(query)) + "&filter=gguf&sort=downloads&direction=-1&limit=25";
}
std::string hfTreeUrl(const std::string& repo) { return "https://huggingface.co/api/models/" + repo + "/tree/main"; }
std::string ollamaSearchUrl(const std::string& query) { return "https://ollama.com/search?q=" + encode(core::trim(query)); }
std::string ollamaTagsUrl(const std::string& name) { return "https://ollama.com/library/" + name + "/tags"; }

std::vector<Entry> parseHfSearch(const std::string& json) {
  std::vector<Entry> out;
  Json j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_array()) return out;
  for (const auto& m : j) {
    if (!m.is_object()) continue;
    Entry e;
    e.source = "hf";
    e.id = m.value("id", m.value("modelId", ""));
    if (e.id.empty()) continue;
    e.downloads = m.contains("downloads") && m["downloads"].is_number() ? m["downloads"].get<long>() : 0;
    e.likes = m.contains("likes") && m["likes"].is_number() ? m["likes"].get<long>() : 0;
    std::string pipeline = m.contains("pipeline_tag") && m["pipeline_tag"].is_string() ? m["pipeline_tag"].get<std::string>() : "";
    e.description = (pipeline.empty() ? "" : pipeline + " - ") + std::to_string(e.downloads) + " downloads, " + std::to_string(e.likes) + " likes";
    out.push_back(e);
  }
  return out;
}

std::string quantOf(const std::string& fileName) {
  std::string stem = fileName;
  const size_t dot = stem.rfind(".gguf");
  if (dot != std::string::npos) stem = stem.substr(0, dot);
  // The quant is the last dash- or dot-separated token that looks like one: q4_k_m, Q8_0, IQ4_XS, f16, bf16, fp16.
  std::string best;
  std::string token;
  auto consider = [&] {
    const std::string u = upper(token);
    const bool quant = (u.size() >= 2 && u[0] == 'Q' && isdigit(static_cast<unsigned char>(u[1]))) ||
                       (u.size() >= 3 && u[0] == 'I' && u[1] == 'Q' && isdigit(static_cast<unsigned char>(u[2]))) || u == "F16" || u == "BF16" ||
                       u == "FP16" || u == "F32";
    if (quant) best = u;
    token.clear();
  };
  for (char c : stem) {
    if (c == '-' || c == '.') consider();
    else token += c;
  }
  consider();
  return best;
}

std::vector<Variant> parseHfTree(const std::string& repo, const std::string& json) {
  std::vector<Variant> out;
  Json j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_array()) return out;
  for (const auto& f : j) {
    if (!f.is_object()) continue;
    const std::string path = f.value("path", "");
    if (!core::endsWith(core::lower(path), ".gguf")) continue;
    if (path.find("-of-") != std::string::npos) continue;   // split files need the whole set; Ollama handles those by tag only when complete
    Variant v;
    v.file = path;
    v.label = quantOf(path);
    if (v.label.empty()) v.label = path;
    v.bytes = f.contains("size") && f["size"].is_number() ? f["size"].get<double>() : -1.0;
    v.pullName = "hf.co/" + repo + (quantOf(path).empty() ? "" : ":" + quantOf(path));
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const Variant& a, const Variant& b) { return a.bytes < b.bytes; });
  return out;
}

std::vector<Entry> parseOllamaSearch(const std::string& html) {
  std::vector<Entry> out;
  std::set<std::string> seen;
  size_t at = 0;
  while ((at = html.find("href=\"/library/", at)) != std::string::npos) {
    const size_t start = at + 15;
    const size_t end = html.find('"', start);
    if (end == std::string::npos) break;
    const std::string name = html.substr(start, end - start);
    at = end;
    if (name.empty() || name.find(':') != std::string::npos || name.find('/') != std::string::npos || seen.count(name)) continue;
    seen.insert(name);
    Entry e;
    e.source = "ollama";
    e.id = name;
    // The description sits in the next <p ... title="..."> after the link.
    const size_t p = html.find("<p ", end);
    const size_t nextLink = html.find("href=\"/library/", end);
    if (p != std::string::npos && (nextLink == std::string::npos || p < nextLink)) {
      const size_t tagEnd = html.find('>', p);
      if (tagEnd != std::string::npos) {
        const std::string tag = html.substr(p, tagEnd - p);
        std::string desc = attrOf(tag, "title");
        if (desc.empty()) {
          const size_t close = html.find("</p>", tagEnd);
          if (close != std::string::npos) desc = html.substr(tagEnd + 1, close - tagEnd - 1);
        }
        e.description = core::trim(decodeEntities(desc));
      }
    }
    out.push_back(e);
  }
  return out;
}

double parseSize(const std::string& textIn) {
  const std::string text = core::trim(textIn);
  char* end = nullptr;
  const double value = strtod(text.c_str(), &end);
  if (end == text.c_str()) return -1.0;
  const std::string unit = upper(core::trim(std::string(end)));
  if (unit == "GB") return value * 1e9;
  if (unit == "MB") return value * 1e6;
  if (unit == "KB") return value * 1e3;
  if (unit == "TB") return value * 1e12;
  if (unit.empty() || unit == "B") return value;
  return -1.0;
}

std::string humanSize(double bytes) {
  if (bytes < 0) return "?";
  char buf[32];
  if (bytes >= 1e9) snprintf(buf, sizeof buf, "%.1f GB", bytes / 1e9);
  else snprintf(buf, sizeof buf, "%.0f MB", bytes / 1e6);
  return buf;
}

std::vector<Variant> parseOllamaTags(const std::string& name, const std::string& html) {
  std::vector<Variant> out;
  std::set<std::string> seen;
  const std::string needle = "href=\"/library/" + name + ":";
  size_t at = 0;
  while ((at = html.find(needle, at)) != std::string::npos) {
    const size_t start = at + needle.size();
    const size_t end = html.find('"', start);
    if (end == std::string::npos) break;
    const std::string tag = html.substr(start, end - start);
    at = end;
    if (tag.empty() || seen.count(tag)) continue;
    seen.insert(tag);
    Variant v;
    v.label = tag;
    v.pullName = name + ":" + tag;
    // The first "<number>GB|MB" after the link is this tag's size.
    size_t p = end;
    const size_t nextLink = html.find("href=\"/library/", end + 1);
    while (p < html.size() && (nextLink == std::string::npos || p < nextLink)) {
      const size_t g = html.find("GB", p), m = html.find("MB", p);
      size_t unit = std::min(g == std::string::npos ? html.size() : g, m == std::string::npos ? html.size() : m);
      if (unit >= html.size() || (nextLink != std::string::npos && unit > nextLink)) break;
      size_t numStart = unit;
      while (numStart > 0 && (isdigit(static_cast<unsigned char>(html[numStart - 1])) || html[numStart - 1] == '.')) --numStart;
      if (numStart < unit) {
        v.bytes = parseSize(html.substr(numStart, unit + 2 - numStart));
        break;
      }
      p = unit + 2;
    }
    out.push_back(v);
  }
  return out;
}

bool enoughSpace(double bytes, double freeBytes) {
  if (bytes < 0 || freeBytes < 0) return true;   // unknown: let Ollama decide
  return freeBytes > bytes * 1.1 + 512e6;
}

Progress parseProgress(const Json& line) {
  Progress p;
  if (!line.is_object()) return p;
  if (line.contains("error")) {
    p.error = line["error"].is_string() ? line["error"].get<std::string>() : line["error"].dump();
    return p;
  }
  p.status = line.value("status", "");
  if (line.contains("completed") && line["completed"].is_number()) p.completed = line["completed"].get<double>();
  if (line.contains("total") && line["total"].is_number()) p.total = line["total"].get<double>();
  p.done = p.status == "success";
  return p;
}

}  // namespace modellib
