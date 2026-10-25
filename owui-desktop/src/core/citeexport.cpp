#include "citeexport.h"

#include <algorithm>
#include <regex>
#include <set>

#include "common.h"

namespace citeexport {
namespace {

std::string str(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

std::string bibEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '{' || c == '}' || c == '%' || c == '&' || c == '#' || c == '_') out += '\\';
    out += c;
  }
  return out;
}

std::string keyFor(const std::string& title, int n) {
  std::string key;
  for (unsigned char c : title) {
    if (isalnum(c)) key += static_cast<char>(tolower(c));
    if (key.size() >= 12) break;
  }
  if (key.empty()) key = "source";
  return key + std::to_string(n);
}

std::string singleLine(std::string s) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }
  return core::trim(s);
}

}  // namespace

std::vector<Source> fromServerSources(const Json& sources) {
  std::vector<Source> out;
  if (!sources.is_array()) return out;
  std::set<std::string> seen;
  for (const auto& entry : sources) {
    Source s;
    if (entry.is_string()) {
      s.title = entry.get<std::string>();
      s.kind = core::startsWith(s.title, "http") ? "web" : "file";
      if (s.kind == "web") s.url = s.title;
    } else if (entry.is_object()) {
      const Json src = entry.value("source", Json::object());
      s.title = str(src, "name");
      s.url = str(src, "url");
      if (s.url.empty() && core::startsWith(s.title, "http")) s.url = s.title;
      const std::string type = str(src, "type");
      s.kind = !s.url.empty() ? "web" : "file";
      if (type == "web_search" || type == "web") s.kind = "web";
      const Json docs = entry.value("document", Json::array());
      if (docs.is_array() && !docs.empty() && docs[0].is_string()) {
        s.snippet = singleLine(docs[0].get<std::string>());
        if (s.snippet.size() > 240) s.snippet = s.snippet.substr(0, 240) + "...";
      }
      const Json metas = entry.value("metadata", Json::array());
      if (metas.is_array() && !metas.empty() && metas[0].is_object()) {
        const Json& m = metas[0];
        if (s.title.empty()) s.title = str(m, "name");
        if (s.title.empty()) s.title = str(m, "source");
        if (m.contains("page") && m["page"].is_number_integer()) s.page = m["page"].get<int>() + 1;   // the server counts from 0
      }
    } else {
      continue;
    }
    if (s.title.empty() && s.url.empty()) continue;
    if (s.title.empty()) s.title = s.url;
    const std::string key = core::lower(s.url.empty() ? s.title : s.url) + "#" + std::to_string(s.page);
    if (seen.count(key)) continue;
    seen.insert(key);
    out.push_back(s);
  }
  return out;
}

std::vector<Source> fromLocalCitations(const std::string& content, const std::vector<std::string>& docNames) {
  std::vector<Source> out;
  if (docNames.empty()) return out;
  static const std::regex kRef(R"(\[([^\[\]\n]{1,120}?)[ ,]+(?:p\.|pp\.|page|slide|part|sheet)\s*([0-9]+)\])", std::regex::icase);
  std::set<std::string> seen;
  for (auto it = std::sregex_iterator(content.begin(), content.end(), kRef); it != std::sregex_iterator(); ++it) {
    const std::string named = core::lower(core::trim((*it)[1].str()));
    const int page = atoi((*it)[2].str().c_str());
    std::string match;
    for (const std::string& d : docNames) {
      const std::string lower = core::lower(d);
      const std::string stem = lower.substr(0, lower.rfind('.'));
      if (lower == named || stem == named || lower.find(named) != std::string::npos || named.find(stem) != std::string::npos) {
        match = d;
        break;
      }
    }
    if (match.empty()) continue;
    const std::string key = core::lower(match) + "#" + std::to_string(page);
    if (seen.count(key)) continue;
    seen.insert(key);
    Source s;
    s.kind = "document";
    s.title = match;
    s.page = page;
    out.push_back(s);
  }
  return out;
}

std::string markdownWithFootnotes(const std::string& content, const std::vector<Source>& server, const std::vector<Source>& local) {
  std::string body = content;
  // Server citations: "[3]" -> "[^3]" when such a source exists (never inside a footnote already).
  if (!server.empty()) {
    static const std::regex kNum(R"(\[(\d{1,3})\])");
    std::string rebuilt;
    auto last = content.cbegin();
    for (auto it = std::sregex_iterator(content.begin(), content.end(), kNum); it != std::sregex_iterator(); ++it) {
      const std::smatch& m = *it;
      const int n = atoi(m[1].str().c_str());
      rebuilt.append(last, m[0].first);
      if (n >= 1 && n <= static_cast<int>(server.size())) rebuilt += "[^" + std::to_string(n) + "]";
      else rebuilt += m[0].str();
      last = m[0].second;
    }
    rebuilt.append(last, content.cend());
    body = rebuilt;
  }
  // Local citations: each distinct one gets the next number.
  const size_t base = server.size();
  for (size_t i = 0; i < local.size(); ++i) {
    const std::string pageText = std::to_string(local[i].page);
    const std::string lowerTitle = core::lower(local[i].title);
    const std::string stem = lowerTitle.substr(0, lowerTitle.rfind('.'));
    // Replace any "[<something naming this doc> p.N]" form with the footnote.
    static const std::regex kRef(R"(\[([^\[\]\n]{1,120}?)[ ,]+(?:p\.|pp\.|page|slide|part|sheet)\s*([0-9]+)\])", std::regex::icase);
    std::string rebuilt;
    auto last = body.cbegin();
    for (auto it = std::sregex_iterator(body.begin(), body.end(), kRef); it != std::sregex_iterator(); ++it) {
      const std::smatch& m = *it;
      const std::string named = core::lower(core::trim(m[1].str()));
      const bool same = (lowerTitle == named || stem == named || lowerTitle.find(named) != std::string::npos || named.find(stem) != std::string::npos) &&
                        m[2].str() == pageText;
      rebuilt.append(last, m[0].first);
      rebuilt += same ? "[^" + std::to_string(base + i + 1) + "]" : m[0].str();
      last = m[0].second;
    }
    rebuilt.append(last, body.cend());
    body = rebuilt;
  }
  if (server.empty() && local.empty()) return body;
  std::string out = core::trim(body) + "\n\n";
  size_t n = 1;
  for (const Source& s : server) {
    out += "[^" + std::to_string(n++) + "]: " + s.title;
    if (!s.url.empty() && s.url != s.title) out += " - " + s.url;
    if (s.page > 0) out += ", p. " + std::to_string(s.page);
    if (!s.snippet.empty()) out += " - \"" + s.snippet.substr(0, 160) + (s.snippet.size() > 160 ? "..." : "") + "\"";
    out += "\n";
  }
  for (const Source& s : local) {
    out += "[^" + std::to_string(n++) + "]: " + s.title + ", p. " + std::to_string(s.page) + " (attached document)\n";
  }
  return out;
}

std::string bibtex(const std::vector<Source>& sources, const std::string& accessedDate) {
  std::string out;
  int n = 1;
  for (const Source& s : sources) {
    out += "@misc{" + keyFor(s.title, n++) + ",\n  title = {" + bibEscape(s.title) + "},\n";
    if (!s.url.empty()) out += "  howpublished = {\\url{" + s.url + "}},\n";
    std::string note;
    if (s.page > 0) note += "page " + std::to_string(s.page);
    if (s.kind == "document") note += (note.empty() ? "" : ", ") + std::string("attached document");
    if (!s.url.empty() && !accessedDate.empty()) note += (note.empty() ? "" : ", ") + std::string("accessed ") + accessedDate;
    if (!note.empty()) out += "  note = {" + bibEscape(note) + "},\n";
    if (!accessedDate.empty()) out += "  year = {" + accessedDate.substr(0, 4) + "}\n";
    else out.erase(out.size() - 2);   // drop the trailing ",\n" of the last field
    out += "}\n\n";
  }
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out + (out.empty() ? "" : "\n");
}

std::string plainList(const std::vector<Source>& sources) {
  std::string out = sources.empty() ? "" : "Sources:\n";
  int n = 1;
  for (const Source& s : sources) {
    out += std::to_string(n++) + ". " + s.title;
    if (!s.url.empty() && s.url != s.title) out += " - " + s.url;
    if (s.page > 0) out += " (p. " + std::to_string(s.page) + ")";
    out += "\n";
  }
  return out;
}

}  // namespace citeexport
