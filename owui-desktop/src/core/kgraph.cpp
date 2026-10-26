#include "kgraph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace kgraph {
namespace {

const std::set<std::string>& stopWords() {
  static const std::set<std::string> words{
      "the",  "a",     "an",    "and",   "or",    "but",   "if",    "then",  "when",  "while",  "this",  "that",   "these", "those",  "it",      "its",    "i",     "we",
      "you",  "he",    "she",   "they",  "them",  "our",   "your",  "my",    "his",   "her",    "their", "is",     "are",   "was",    "were",    "be",     "been",  "do",
      "does", "did",   "has",   "have",  "had",   "not",   "no",    "yes",   "so",    "as",     "at",    "by",     "for",   "from",   "in",      "into",   "of",    "on",
      "to",   "with",  "about", "after", "before", "over", "under", "again", "also",  "every",  "each",  "all",    "any",   "some",   "there",   "here",   "how",   "what",
      "why",  "who",   "which", "use",   "using", "used",  "note",  "notes", "memory", "today", "tomorrow", "yesterday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday", "mondays", "tuesdays", "wednesdays", "thursdays", "fridays", "saturdays", "sundays",
      "first", "last", "next",  "new",   "one",   "two",   "three", "can",   "will",  "would",  "should", "could", "may",   "might",  "must",    "just",   "very",  "more"};
  return words;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool isWordChar(unsigned char c) { return std::isalnum(c) || c == '\'' || c == '-' || c >= 0x80; }

struct Word {
  std::string text;
  bool capital = false;
  bool sentenceStart = false;
  size_t pos = 0;
};

std::vector<Word> words(const std::string& text) {
  std::vector<Word> out;
  size_t i = 0;
  bool sentenceStart = true;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (isWordChar(c)) {
      size_t j = i;
      while (j < text.size() && isWordChar(static_cast<unsigned char>(text[j]))) ++j;
      Word w;
      w.text = text.substr(i, j - i);
      while (!w.text.empty() && (w.text.back() == '\'' || w.text.back() == '-')) w.text.pop_back();
      w.capital = !w.text.empty() && std::isupper(static_cast<unsigned char>(w.text[0]));
      w.sentenceStart = sentenceStart;
      w.pos = i;
      if (!w.text.empty()) out.push_back(w);
      sentenceStart = false;
      i = j;
    } else {
      if (c == '.' || c == '!' || c == '?' || c == '\n' || c == ':' || c == ';' || c == '(' || c == ')' || c == '"' || c == '-' || c == ',' ) {
        if (c != ',' && c != '-') sentenceStart = true;
      }
      ++i;
    }
  }
  return out;
}

}  // namespace

std::vector<std::string> extractEntities(const std::string& text) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  auto add = [&](std::string label) {
    while (!label.empty() && std::isspace(static_cast<unsigned char>(label.back()))) label.pop_back();
    if (label.size() < 3) return;
    const std::string key = lower(label);
    if (stopWords().count(key)) return;
    if (seen.insert(key).second) out.push_back(label);
  };
  // #tags and `code terms` first.
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '#' && i + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[i + 1])) && (i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1])))) {
      size_t j = i + 1;
      while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_' || text[j] == '-')) ++j;
      add("#" + text.substr(i + 1, j - i - 1));
      i = j;
    } else if (text[i] == '`') {
      const size_t j = text.find('`', i + 1);
      if (j == std::string::npos) break;
      const std::string term = text.substr(i + 1, j - i - 1);
      if (term.find('\n') == std::string::npos && term.size() <= 40) add(term);
      i = j;
    }
  }
  // Capitalised runs.
  const std::vector<Word> ws = words(text);
  size_t i = 0;
  while (i < ws.size()) {
    if (!ws[i].capital || stopWords().count(lower(ws[i].text))) {
      ++i;
      continue;
    }
    size_t j = i;
    std::string run;
    while (j < ws.size() && j - i < 4 && ws[j].capital && !stopWords().count(lower(ws[j].text)) && (j == i || ws[j].pos <= ws[j - 1].pos + ws[j - 1].text.size() + 1)) {
      run += (j == i ? "" : " ") + ws[j].text;
      ++j;
    }
    // A lone capitalised word that only starts a sentence is just a sentence start.
    const bool lone = j == i + 1;
    if (!(lone && ws[i].sentenceStart && ws[i].text.size() < 12 && std::islower(static_cast<unsigned char>(ws[i].text.size() > 1 ? ws[i].text[1] : 'a')) && lower(ws[i].text) == lower(ws[i].text))) {
      if (!(lone && ws[i].sentenceStart)) add(run);
      else if (ws[i].text.size() >= 2 && std::all_of(ws[i].text.begin() + 1, ws[i].text.end(), [](unsigned char c) { return std::isupper(c) || std::isdigit(c); })) add(run);   // an acronym at a sentence start still counts
    }
    i = j;
  }
  return out;
}

Graph build(const std::vector<Source>& sources, const Options& options) {
  Graph g;
  std::map<std::string, Node> entities;   // lower label -> node
  std::map<std::string, std::vector<std::string>> perSource;   // source id -> entity keys
  for (const Source& s : sources) {
    Node sn;
    sn.id = s.id;
    sn.label = s.title.empty() ? s.kind : s.title;
    sn.kind = s.kind;
    g.nodes.push_back(sn);
    std::set<std::string> here;
    const bool titled = s.kind != "memory";   // a memory's title is a cut of its text
    for (const std::string& label : extractEntities((titled ? s.title + "\n" : std::string()) + s.text)) {
      const std::string key = lower(label);
      Node& e = entities[key];
      if (e.id.empty()) {
        e.id = "entity:" + key;
        e.label = label;
        e.kind = "entity";
        e.weight = 0;
      }
      ++e.weight;
      if (here.insert(key).second) {
        e.sources.push_back(s.id);
        perSource[s.id].push_back(key);
      }
    }
  }
  // Keep entities with enough mentions, then the most mentioned.
  std::vector<Node> kept;
  for (auto& [key, e] : entities) {
    if (e.weight >= options.minMentions || static_cast<int>(e.sources.size()) >= options.minMentions) kept.push_back(e);
  }
  std::stable_sort(kept.begin(), kept.end(), [](const Node& a, const Node& b) { return a.weight != b.weight ? a.weight > b.weight : a.label < b.label; });
  if (static_cast<int>(kept.size()) > options.maxEntities) kept.resize(static_cast<size_t>(options.maxEntities));
  std::set<std::string> keptKeys;
  for (const Node& e : kept) {
    keptKeys.insert(lower(e.label));
    g.nodes.push_back(e);
  }
  // Edges: source -> entity, and entity-entity co-occurrence.
  std::map<std::pair<std::string, std::string>, int> co;
  for (auto& [sid, keys] : perSource) {
    std::vector<std::string> present;
    for (const std::string& k : keys) {
      if (keptKeys.count(k)) present.push_back(k);
    }
    const int si = indexOf(g, sid);
    if (si >= 0) g.nodes[static_cast<size_t>(si)].weight = static_cast<int>(present.size());
    for (const std::string& k : present) g.edges.push_back(Edge{sid, "entity:" + k, 1, false});
    if (options.coOccurrence) {
      for (size_t a = 0; a < present.size(); ++a) {
        for (size_t b = a + 1; b < present.size(); ++b) {
          auto pair = std::minmax(present[a], present[b]);
          ++co[{pair.first, pair.second}];
        }
      }
    }
  }
  for (const auto& [pair, weight] : co) g.edges.push_back(Edge{"entity:" + pair.first, "entity:" + pair.second, weight, true});
  return g;
}

int indexOf(const Graph& g, const std::string& id) {
  for (size_t i = 0; i < g.nodes.size(); ++i) {
    if (g.nodes[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

std::vector<std::string> neighbours(const Graph& g, const std::string& id) {
  std::vector<std::string> out;
  for (const Edge& e : g.edges) {
    if (e.a == id) out.push_back(e.b);
    else if (e.b == id) out.push_back(e.a);
  }
  return out;
}

void seedLayout(Graph& g, float width, float height) {
  const float cx = width * 0.5f, cy = height * 0.5f;
  size_t entityCount = 0, sourceCount = 0;
  for (const Node& n : g.nodes) (n.kind == "entity" ? entityCount : sourceCount)++;
  size_t ei = 0, si = 0;
  for (Node& n : g.nodes) {
    const bool entity = n.kind == "entity";
    const size_t count = entity ? entityCount : sourceCount;
    const size_t index = entity ? ei++ : si++;
    const float radius = (entity ? 0.42f : 0.18f) * std::min(width, height);
    const float angle = count ? static_cast<float>(index) / static_cast<float>(count) * 6.2831853f : 0.0f;
    n.x = cx + std::cos(angle) * radius;
    n.y = cy + std::sin(angle) * radius;
    n.vx = n.vy = 0.0f;
  }
}

float layoutStep(Graph& g, float width, float height, float dt) {
  const size_t n = g.nodes.size();
  if (n == 0) return 0.0f;
  std::vector<float> fx(n, 0.0f), fy(n, 0.0f);
  const float repulsion = 3600.0f, spring = 0.015f, restLength = 110.0f, gravity = 0.010f;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      float dx = g.nodes[i].x - g.nodes[j].x, dy = g.nodes[i].y - g.nodes[j].y;
      float d2 = dx * dx + dy * dy;
      if (d2 < 1.0f) {
        dx = static_cast<float>((i * 7 + 3) % 11) - 5.0f;
        dy = static_cast<float>((j * 5 + 1) % 11) - 5.0f;
        d2 = dx * dx + dy * dy + 1.0f;
      }
      const float f = repulsion / d2;
      const float d = std::sqrt(d2);
      fx[i] += f * dx / d;
      fy[i] += f * dy / d;
      fx[j] -= f * dx / d;
      fy[j] -= f * dy / d;
    }
  }
  for (const Edge& e : g.edges) {
    const int a = indexOf(g, e.a), b = indexOf(g, e.b);
    if (a < 0 || b < 0) continue;
    const float dx = g.nodes[static_cast<size_t>(b)].x - g.nodes[static_cast<size_t>(a)].x, dy = g.nodes[static_cast<size_t>(b)].y - g.nodes[static_cast<size_t>(a)].y;
    const float d = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
    const float f = spring * (d - restLength) * (e.coOccurrence ? 0.5f : 1.0f) * std::min(3.0f, static_cast<float>(e.weight));
    fx[static_cast<size_t>(a)] += f * dx / d;
    fy[static_cast<size_t>(a)] += f * dy / d;
    fx[static_cast<size_t>(b)] -= f * dx / d;
    fy[static_cast<size_t>(b)] -= f * dy / d;
  }
  const float cx = width * 0.5f, cy = height * 0.5f;
  float largest = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    Node& node = g.nodes[i];
    if (node.pinned) continue;
    fx[i] += (cx - node.x) * gravity;
    fy[i] += (cy - node.y) * gravity;
    node.vx = (node.vx + fx[i] * dt) * 0.82f;
    node.vy = (node.vy + fy[i] * dt) * 0.82f;
    const float maxV = 40.0f;
    node.vx = std::clamp(node.vx, -maxV, maxV);
    node.vy = std::clamp(node.vy, -maxV, maxV);
    node.x = std::clamp(node.x + node.vx, 20.0f, std::max(20.0f, width - 20.0f));
    node.y = std::clamp(node.y + node.vy, 20.0f, std::max(20.0f, height - 20.0f));
    largest = std::max(largest, std::abs(node.vx) + std::abs(node.vy));
  }
  return largest;
}

std::string stats(const Graph& g) {
  int entities = 0, sources = 0;
  for (const Node& n : g.nodes) (n.kind == "entity" ? entities : sources)++;
  return std::to_string(entities) + (entities == 1 ? " entity, " : " entities, ") + std::to_string(sources) + (sources == 1 ? " source, " : " sources, ") + std::to_string(g.edges.size()) + (g.edges.size() == 1 ? " link" : " links");
}

Json toJson(const Graph& g) {
  Json nodes = Json::array(), edges = Json::array();
  for (const Node& n : g.nodes) nodes.push_back(Json{{"id", n.id}, {"label", n.label}, {"kind", n.kind}, {"weight", n.weight}, {"sources", n.sources}, {"x", n.x}, {"y", n.y}});
  for (const Edge& e : g.edges) edges.push_back(Json{{"source", e.a}, {"target", e.b}, {"weight", e.weight}, {"coOccurrence", e.coOccurrence}});
  return Json{{"nodes", nodes}, {"edges", edges}};
}

}  // namespace kgraph
