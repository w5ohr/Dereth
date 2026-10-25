#include "msgselect.h"

#include <algorithm>

#include "common.h"

namespace msgselect {

std::vector<std::string> rangeBetween(const std::vector<std::string>& path, const std::string& anchor, const std::string& target) {
  const auto a = std::find(path.begin(), path.end(), anchor);
  const auto t = std::find(path.begin(), path.end(), target);
  if (t == path.end()) return {};
  if (a == path.end()) return {target};
  const auto lo = std::min(a, t), hi = std::max(a, t);
  return std::vector<std::string>(lo, hi + 1);
}

bool spliceOut(Graph* graph, const std::string& id, std::string* currentId) {
  auto it = graph->find(id);
  if (it == graph->end()) return false;
  const Node node = it->second;
  const bool hasParent = !node.parentId.empty() && graph->count(node.parentId);
  if (hasParent) {
    std::vector<std::string>& siblings = (*graph)[node.parentId].children;
    auto at = std::find(siblings.begin(), siblings.end(), id);
    if (at != siblings.end()) {
      at = siblings.erase(at);
      siblings.insert(at, node.children.begin(), node.children.end());
    } else {
      siblings.insert(siblings.end(), node.children.begin(), node.children.end());
    }
  }
  for (const std::string& child : node.children) {
    auto c = graph->find(child);
    if (c != graph->end()) c->second.parentId = hasParent ? node.parentId : "";
  }
  graph->erase(it);
  if (currentId && *currentId == id) {
    *currentId = !node.children.empty() ? node.children.back() : (hasParent ? node.parentId : "");
  }
  return true;
}

std::string markdown(const Graph& graph, const std::vector<std::string>& ids) {
  std::string out;
  for (const std::string& id : ids) {
    auto it = graph.find(id);
    if (it == graph.end()) continue;
    const Node& n = it->second;
    out += "## " + (n.who.empty() ? (n.role == "user" ? "You" : "Assistant") : n.who);
    if (n.timestamp > 0) out += "  _" + core::formatTime(n.timestamp) + "_";
    out += "\n\n" + n.content + "\n\n";
  }
  return out;
}

std::vector<Node> linearChain(const Graph& graph, const std::vector<std::string>& ids, const std::vector<std::string>& newIds) {
  std::vector<Node> out;
  for (size_t i = 0; i < ids.size() && i < newIds.size(); ++i) {
    auto it = graph.find(ids[i]);
    if (it == graph.end()) continue;
    Node n = it->second;
    n.id = newIds[i];
    n.children.clear();
    n.parentId = out.empty() ? "" : out.back().id;
    if (!out.empty()) out.back().children = {n.id};
    out.push_back(n);
  }
  return out;
}

}  // namespace msgselect
