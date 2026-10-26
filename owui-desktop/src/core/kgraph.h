// Knowledge graph (#108): entities pulled out of memories, notes and
// knowledge-base files, linked to the sources that mention them and to each
// other when they share a source, laid out with a small force simulation.
// App-free; --graphtest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kgraph {

using Json = nlohmann::json;

struct Source {
  std::string id;      // unique across kinds (kind:serverId)
  std::string kind;    // memory | note | knowledge
  std::string title;
  std::string text;
};

struct Node {
  std::string id;      // entity:<label> or the source id
  std::string label;
  std::string kind;    // entity | memory | note | knowledge
  int weight = 1;      // entity: mentions; source: entities it carries
  float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
  bool pinned = false;
  std::vector<std::string> sources;   // entity: the source ids that mention it
};

struct Edge {
  std::string a, b;
  int weight = 1;
  bool coOccurrence = false;   // entity-entity (same source) rather than source-entity
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};

struct Options {
  int minMentions = 1;        // an entity needs this many mentioning sources (or mentions in one)
  int maxEntities = 80;       // keep the most mentioned
  bool coOccurrence = true;   // entity-entity links
};

// Entities in a text: capitalised runs of one to four words that are not
// stop words, #tags and `code terms`, each once, in order of appearance.
std::vector<std::string> extractEntities(const std::string& text);
Graph build(const std::vector<Source>& sources, const Options& options);
int indexOf(const Graph& g, const std::string& id);        // -1 when absent
std::vector<std::string> neighbours(const Graph& g, const std::string& id);
void seedLayout(Graph& g, float width, float height);     // a ring, sources inside
// One force step: repulsion between nodes, springs along edges, pull to the
// centre, velocities damped and nodes kept inside the box. Returns the
// largest move, so callers can stop once it settles.
float layoutStep(Graph& g, float width, float height, float dt);
std::string stats(const Graph& g);                         // "12 entities, 5 sources, 23 links"
Json toJson(const Graph& g);

}  // namespace kgraph
