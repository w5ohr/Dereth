// Message selection (#70): the pure parts of selecting a range of messages
// in a chat, splicing selected ones out of the tree (children re-attach to
// the parent, so the rest of the conversation survives), and rendering a
// selection as Markdown. The chat graph is passed as plain nodes so this
// stays app-free; --selectiontest covers it.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace msgselect {

struct Node {
  std::string id, parentId, role, who, content;
  std::vector<std::string> children;
  int64_t timestamp = 0;
};
using Graph = std::map<std::string, Node>;

// The ids of `path` between `anchor` and `target` inclusive, in path order;
// just `target` when the anchor is not on the path.
std::vector<std::string> rangeBetween(const std::vector<std::string>& path, const std::string& anchor, const std::string& target);

// Removes `id`, re-parenting its children to its parent (or making them
// roots). `currentId` moves to the last child, else the parent, when it was
// the removed node. Returns false when the id is unknown.
bool spliceOut(Graph* graph, const std::string& id, std::string* currentId);

// "## You  _time_\n\ntext\n\n## Assistant ..." for the given ids, in order.
std::string markdown(const Graph& graph, const std::vector<std::string>& ids);

// Copies of the given nodes as a fresh linear chain with new ids: the body
// of a chat to create from a selection.
std::vector<Node> linearChain(const Graph& graph, const std::vector<std::string>& ids, const std::vector<std::string>& newIds);

}  // namespace msgselect
