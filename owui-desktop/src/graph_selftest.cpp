// `Open WebUI --graphtest`: offline checks of the knowledge graph.
#include <algorithm>
#include <cstdio>
#include <string>

#include "core/kgraph.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) out += s + "|";
  return out;
}
}  // namespace

int runGraphSelfTest() {
  using namespace kgraph;
  printf("Open WebUI desktop knowledge-graph self-test\n\n");

  const auto e = extractEntities("The lighthouse keeper Ada Lovelace polishes the Fresnel Lens every Monday. Ada also tests the foghorn with Captain Reyes. Tag it #maintenance and run `grease-winch`. The end.");
  report("extractEntities: multi-word names, acronym-free terms, tags, code; stop words and sentence starts dropped",
         has(e, "Ada Lovelace") && has(e, "Fresnel Lens") && has(e, "Captain Reyes") && has(e, "#maintenance") && has(e, "grease-winch") && !has(e, "The") && !has(e, "Monday") && !has(e, "Tag") && !has(e, "Ada also"), join(e));
  report("extractEntities: dedupes case-insensitively, keeps order, acronyms at a sentence start", extractEntities("NASA launched. nasa again. NASA once more.").size() == 1 && extractEntities("Winch Room is here. Winch Room again.")[0] == "Winch Room" &&
                                                                                                        has(extractEntities("GPU budgets matter."), "GPU") && extractEntities("").empty());

  std::vector<Source> sources{{"memory:1", "memory", "", "Ada Lovelace keeps the Fresnel Lens clean."},
                              {"note:2", "note", "Winch notes", "The Winch Room holds the grease. Ada Lovelace signs the log."},
                              {"knowledge:3", "knowledge", "visitors.md", "Captain Reyes brings visitors to the Winch Room on Fridays."}};
  Options o;
  const Graph g = build(sources, o);
  const int ada = indexOf(g, "entity:ada lovelace"), winch = indexOf(g, "entity:winch room");
  report("build: source nodes with titles, entity nodes weighted by mentions, sources listed", g.nodes.size() == 3 + 4 && g.nodes[1].label == "Winch notes" && ada >= 0 && g.nodes[static_cast<size_t>(ada)].weight == 2 &&
                                                                                                   g.nodes[static_cast<size_t>(ada)].sources.size() == 2 && winch >= 0 && g.nodes[static_cast<size_t>(winch)].sources.size() == 2, stats(g));
  int sourceEdges = 0, coEdges = 0, adaWinch = 0;
  for (const Edge& ed : g.edges) {
    (ed.coOccurrence ? coEdges : sourceEdges)++;
    if (ed.coOccurrence && ((ed.a == "entity:ada lovelace" && ed.b == "entity:winch room") || (ed.b == "entity:ada lovelace" && ed.a == "entity:winch room"))) adaWinch = ed.weight;
  }
  report("build: source-entity links and co-occurrence links with weights", sourceEdges == 6 && coEdges == 3 && adaWinch == 1 && neighbours(g, "memory:1").size() == 2 && indexOf(g, "nope") == -1, stats(g));
  Options strict;
  strict.minMentions = 2;
  const Graph g2 = build(sources, strict);
  Options capped;
  capped.maxEntities = 1;
  capped.coOccurrence = false;
  const Graph g3 = build(sources, capped);
  report("options: minMentions prunes, maxEntities keeps the most mentioned, co-occurrence off", g2.nodes.size() == 3 + 2 && g3.nodes.size() == 4 && g3.nodes[3].label == "Ada Lovelace" && std::none_of(g3.edges.begin(), g3.edges.end(), [](const Edge& ed) { return ed.coOccurrence; }));

  Graph l = g;
  seedLayout(l, 800.0f, 600.0f);
  bool inside = true;
  for (const Node& n : l.nodes) inside = inside && n.x >= 0 && n.x <= 800 && n.y >= 0 && n.y <= 600;
  float first = layoutStep(l, 800.0f, 600.0f, 1.0f), last = first;
  for (int i = 0; i < 400; ++i) last = layoutStep(l, 800.0f, 600.0f, 1.0f);
  bool stillInside = true;
  float minDist = 1e9f;
  for (size_t i = 0; i < l.nodes.size(); ++i) {
    stillInside = stillInside && l.nodes[i].x >= 20 && l.nodes[i].x <= 780 && l.nodes[i].y >= 20 && l.nodes[i].y <= 580;
    for (size_t j = i + 1; j < l.nodes.size(); ++j) {
      const float dx = l.nodes[i].x - l.nodes[j].x, dy = l.nodes[i].y - l.nodes[j].y;
      minDist = std::min(minDist, dx * dx + dy * dy);
    }
  }
  report("layout: seeded inside, settles (last move below first), stays inside, nodes apart", inside && last < first && last < 2.0f && stillInside && minDist > 100.0f, "first " + std::to_string(first) + " last " + std::to_string(last));
  Graph pinned = l;
  pinned.nodes[0].pinned = true;
  const float px = pinned.nodes[0].x;
  layoutStep(pinned, 800.0f, 600.0f, 1.0f);
  report("layout: a pinned node does not move; empty graph is a no-op", pinned.nodes[0].x == px && layoutStep(*new Graph(), 10, 10, 1) == 0.0f);
  const Json j = toJson(g);
  report("stats / toJson", stats(g) == "4 entities, 3 sources, " + std::to_string(g.edges.size()) + " links" && j["nodes"].size() == 7 && j["edges"].size() == g.edges.size() && j["nodes"][0]["kind"] == "memory");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
