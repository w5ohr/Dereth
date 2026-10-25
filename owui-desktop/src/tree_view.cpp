// Conversation tree: a map of every branch in the chat beside the transcript.
// Nodes are laid out top-down (one row per turn, siblings side by side), the
// branch the transcript shows is highlighted, a click jumps to a node's
// branch, and dead branches - everything off the current path - can be
// pruned one subtree at a time or all at once.
#include "app.h"

#include <algorithm>
#include <functional>
#include <map>

#include "core/async.h"
#include "core/common.h"
#include "imgui_internal.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr float kNodeWidth = 150.0f;
constexpr float kNodeHeight = 38.0f;
constexpr float kHGap = 14.0f;
constexpr float kVGap = 30.0f;
constexpr float kPad = 24.0f;

float gZoom = 1.0f;
std::string gLastCurrent;      // recentre when the branch changes
bool gScrollToCurrent = false;
size_t gFittedCount = 0;       // node count the zoom was last fitted to

struct Node {
  std::string id;
  int depth = 0;
  float x = 0.0f;              // centre, unscaled
  float width = 0.0f;          // subtree width, unscaled
  std::vector<std::string> children;
};

struct Layout {
  std::map<std::string, Node> nodes;
  std::vector<std::string> roots;
  float width = 0.0f;
  int depth = 0;
};

// Children in the order they were created, dropping dangling ids.
std::vector<std::string> childrenOf(const ChatDoc& chat, const Message& message) {
  std::vector<std::string> out;
  for (const std::string& child : message.childrenIds) {
    if (chat.messages.count(child)) out.push_back(child);
  }
  // Messages whose parent lists them nowhere (older imports) still belong.
  for (const auto& [id, candidate] : chat.messages) {
    if (candidate.parentId == message.id && std::find(out.begin(), out.end(), id) == out.end()) {
      out.push_back(id);
    }
  }
  return out;
}

float measure(const ChatDoc& chat, Layout& layout, const std::string& id, int depth) {
  Node& node = layout.nodes[id];
  node.id = id;
  node.depth = depth;
  layout.depth = std::max(layout.depth, depth);
  node.children = childrenOf(chat, chat.messages.at(id));
  float total = 0.0f;
  for (const std::string& child : node.children) {
    if (total > 0.0f) total += kHGap;
    total += measure(chat, layout, child, depth + 1);
  }
  node.width = std::max(kNodeWidth, total);
  return node.width;
}

void place(Layout& layout, const std::string& id, float left) {
  Node& node = layout.nodes[id];
  node.x = left + node.width * 0.5f;
  float cursor = left + (node.width - [&] {
    float total = 0.0f;
    for (const std::string& child : node.children) {
      if (total > 0.0f) total += kHGap;
      total += layout.nodes[child].width;
    }
    return total;
  }()) * 0.5f;
  for (const std::string& child : node.children) {
    place(layout, child, cursor);
    cursor += layout.nodes[child].width + kHGap;
  }
}

Layout buildLayout(const ChatDoc& chat) {
  Layout layout;
  for (const auto& [id, message] : chat.messages) {
    if (message.parentId.empty() || !chat.messages.count(message.parentId)) layout.roots.push_back(id);
  }
  std::sort(layout.roots.begin(), layout.roots.end(), [&](const std::string& a, const std::string& b) {
    return chat.messages.at(a).timestamp < chat.messages.at(b).timestamp;
  });
  float left = 0.0f;
  for (const std::string& root : layout.roots) {
    if (left > 0.0f) left += kHGap * 2.0f;
    measure(chat, layout, root, 0);
    place(layout, root, left);
    left += layout.nodes[root].width;
  }
  layout.width = left;
  return layout;
}

std::string excerpt(const Message& message, size_t limit) {
  std::string text = md::toPlainText(message.content);
  for (char& c : text) {
    if (c == '\n' || c == '\t') c = ' ';
  }
  text = core::trim(text);
  if (text.empty()) text = message.streaming ? "..." : (message.error.empty() ? "(empty)" : "(error)");
  if (text.size() > limit) text = text.substr(0, limit - 1) + "...";
  return text;
}

}  // namespace

// ---------------------------------------------------------------- actions

// Jumps the transcript to `id`: on the current branch it just scrolls there,
// otherwise the branch through it becomes current (down to its newest leaf).
void App::jumpToNode(const std::string& id) {
  if (!chat.messages.count(id)) return;
  const std::vector<std::string> path = chat.branch();
  if (std::find(path.begin(), path.end(), id) == path.end()) {
    std::string leaf = id;
    for (;;) {
      const std::vector<std::string> kids = childrenOf(chat, chat.messages.at(leaf));
      if (kids.empty()) break;
      leaf = kids.back();
    }
    chat.currentId = leaf;
    gScrollToCurrent = true;
  }
  pendingScrollTo = id;
}

// Removes `id` and everything under it, locally and on the server. A subtree
// that holds the current branch moves the transcript to the node's parent.
void App::pruneSubtree(const std::string& id) {
  auto it = chat.messages.find(id);
  if (it == chat.messages.end()) return;
  std::vector<std::string> doomed;
  std::vector<std::string> stack{id};
  while (!stack.empty()) {
    const std::string current = stack.back();
    stack.pop_back();
    auto node = chat.messages.find(current);
    if (node == chat.messages.end()) continue;
    if (node->second.streaming) {
      widgets::toastInfo("That branch is still being written - stop it first");
      return;
    }
    doomed.push_back(current);
    for (const std::string& child : childrenOf(chat, node->second)) stack.push_back(child);
  }
  const std::string parentId = it->second.parentId;
  const std::vector<std::string> path = chat.branch();
  if (std::find(path.begin(), path.end(), id) != path.end()) chat.currentId = parentId;
  if (!parentId.empty() && chat.messages.count(parentId)) {
    auto& siblings = chat.messages[parentId].childrenIds;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
  }
  for (const std::string& gone : doomed) {
    chat.messages.erase(gone);
    if (isPinned(gone)) togglePin(gone);
  }
  if (chat.currentId.empty() && !chat.messages.empty()) {
    // Pruned a root: land on the newest remaining leaf.
    std::string best;
    for (const auto& [mid, message] : chat.messages) {
      if (childrenOf(chat, message).empty() &&
          (best.empty() || message.timestamp > chat.messages.at(best).timestamp)) {
        best = mid;
      }
    }
    chat.currentId = best;
  }
  // The server merges chat bodies and keeps messages that merely vanished,
  // so each one has to be deleted explicitly - and before the save, since a
  // delete moves the server's currentId to the deleted message's parent.
  if (!chat.id.empty()) {
    const std::string chatId = chat.id;
    async::run([this, chatId, doomed] {
      for (const std::string& gone : doomed) api_.deleteChatMessage(chatId, gone);
      async::post([this, chatId] {
        if (chat.id == chatId) saveActiveChat();
      });
    });
  }
  widgets::toast(widgets::ToastLevel::Success,
                 "Pruned " + std::to_string(doomed.size()) + (doomed.size() == 1 ? " message" : " messages"));
}

// Every subtree hanging off the current branch, in one go.
void App::pruneDeadBranches() {
  const std::vector<std::string> path = chat.branch();
  std::vector<std::string> dead;
  for (const std::string& id : path) {
    for (const std::string& child : childrenOf(chat, chat.messages.at(id))) {
      if (std::find(path.begin(), path.end(), child) == path.end()) dead.push_back(child);
    }
  }
  for (const auto& [id, message] : chat.messages) {
    if ((message.parentId.empty() || !chat.messages.count(message.parentId)) &&
        std::find(path.begin(), path.end(), id) == path.end()) {
      dead.push_back(id);
    }
  }
  if (dead.empty()) return;
  size_t total = 0;
  for (const std::string& id : dead) {
    const size_t before = chat.messages.size();
    pruneSubtree(id);
    total += before - chat.messages.size();
  }
  (void)total;
}

// ---------------------------------------------------------------- drawing

void App::drawTreeColumn(float width) {
  const auto& palette = theme::palette();
  ImGui::BeginChild("##tree", ImVec2(width, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(10.0f);
  widgets::sectionHeader("CONVERSATION TREE");

  const std::vector<std::string> path = chat.branch();
  const Layout layout = buildLayout(chat);
  int leaves = 0, dead = 0;
  for (const auto& [id, node] : layout.nodes) {
    if (!node.children.empty()) continue;
    ++leaves;
    if (std::find(path.begin(), path.end(), id) == path.end()) ++dead;
  }

  // A tree wider than the column opens zoomed out to fit; growing it refits.
  auto fit = [&] {
    const float usable = width - 24.0f;
    gZoom = std::clamp(usable / std::max(1.0f, layout.width + kPad * 2.0f), 0.5f, 1.0f);
  };
  if (gFittedCount != layout.nodes.size()) {
    gFittedCount = layout.nodes.size();
    fit();
    gScrollToCurrent = true;
  }

  // Toolbar: zoom, recentre, prune.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  if (ImGui::SmallButton("-")) gZoom = std::max(0.5f, gZoom - 0.1f);
  ImGui::SameLine();
  if (ImGui::SmallButton("+")) gZoom = std::min(1.6f, gZoom + 0.1f);
  ImGui::SameLine();
  if (ImGui::SmallButton("Fit")) fit();
  ImGui::SameLine();
  if (ImGui::SmallButton("Current")) gScrollToCurrent = true;
  ImGui::SameLine();
  widgets::beginDisabled(dead == 0 || !streams_.empty());
  if (ImGui::SmallButton(dead == 0 ? "Prune dead branches"
                                   : ("Prune " + std::to_string(dead) + (dead == 1 ? " dead branch" : " dead branches")).c_str())) {
    ImGui::OpenPopup("##prunedead");
  }
  widgets::endDisabled(dead == 0 || !streams_.empty());
  ImGui::PopStyleVar();
  if (ImGui::BeginPopup("##prunedead")) {
    ImGui::TextUnformatted("Delete every branch off the current path?");
    widgets::dimText("Messages on other branches are removed from the server too.");
    if (widgets::primaryButton("Prune", ImVec2(90, 0))) {
      pruneDeadBranches();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  widgets::dimText("%zu messages - %d %s - %d dead %s", chat.messages.size(), leaves,
                   leaves == 1 ? "leaf" : "leaves", dead, dead == 1 ? "end" : "ends");
  ImGui::Unindent(10.0f);

  if (chat.messages.empty()) {
    ImGui::Indent(10.0f);
    widgets::dimText("Nothing here yet.");
    ImGui::Unindent(10.0f);
    ImGui::EndChild();
    return;
  }

  // The canvas scrolls both ways; geometry is scaled by the zoom.
  const float z = gZoom;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.bg));
  ImGui::BeginChild("##treecanvas", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::PushFont(nullptr, ImGui::GetFontSize() * std::clamp(z, 0.7f, 1.0f));   // ImGui 1.92: size per push
  // Widths follow the zoom; a node stays tall enough for its two lines.
  const float lineH = ImGui::GetTextLineHeight();
  const float nodeW = kNodeWidth * z;
  const float nodeH = std::max(kNodeHeight * z, lineH * 2.0f + 10.0f * z);
  const float rowH = nodeH + kVGap * z;
  const ImVec2 canvasSize((layout.width + kPad * 2.0f) * z, (layout.depth + 1) * rowH + kPad * 2.0f * z);

  auto centre = [&](const Node& node) {
    return ImVec2(origin.x + (kPad + node.x) * z, origin.y + kPad * z + node.depth * rowH + nodeH * 0.5f);
  };
  auto onPath = [&](const std::string& id) { return std::find(path.begin(), path.end(), id) != path.end(); };

  // Edges first so nodes sit on top of them.
  for (const auto& [id, node] : layout.nodes) {
    const ImVec2 from = centre(node);
    for (const std::string& childId : node.children) {
      const ImVec2 to = centre(layout.nodes.at(childId));
      const bool live = onPath(id) && onPath(childId);
      const ImVec2 a(from.x, from.y + nodeH * 0.5f), b(to.x, to.y - nodeH * 0.5f);
      const float bend = (b.y - a.y) * 0.5f;
      dl->AddBezierCubic(a, ImVec2(a.x, a.y + bend), ImVec2(b.x, b.y - bend), b,
                         live ? palette.accent : palette.border, live ? 2.0f : 1.2f);
    }
  }

  std::string pruneTarget;
  for (const auto& [id, node] : layout.nodes) {
    const Message& message = chat.messages.at(id);
    const ImVec2 c = centre(node);
    const ImVec2 min(c.x - nodeW * 0.5f, c.y - nodeH * 0.5f), max(c.x + nodeW * 0.5f, c.y + nodeH * 0.5f);
    const bool isUser = message.role == "user";
    const bool live = onPath(id);
    const bool current = id == chat.currentId;

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id.c_str());
    ImGui::InvisibleButton("##node", ImVec2(nodeW, nodeH));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) jumpToNode(id);
    if (ImGui::BeginPopupContextItem("##nodemenu")) {
      if (ImGui::MenuItem("Jump here")) jumpToNode(id);
      if (ImGui::MenuItem("Copy text")) platform::setClipboardText(message.content);
      ImGui::Separator();
      widgets::beginDisabled(!streams_.empty());
      if (ImGui::MenuItem(node.children.empty() ? "Delete this message" : "Prune this branch")) pruneTarget = id;
      widgets::endDisabled(!streams_.empty());
      ImGui::EndPopup();
    }
    ImGui::PopID();

    const ImU32 fill = hovered ? palette.panelAlt : (isUser ? palette.userBubble : palette.assistantBubble);
    dl->AddRectFilled(min, max, fill, 6.0f * z);
    if (current) {
      dl->AddRect(min, max, palette.accent, 6.0f * z, 2.5f);
    } else if (live) {
      dl->AddRect(min, max, palette.accent, 6.0f * z, 1.2f);
    } else {
      dl->AddRect(min, max, palette.border, 6.0f * z, 1.0f);
    }

    // Two lines: who (+ sibling position), then the opening words.
    std::string who = isUser ? "You" : (message.modelName.empty() ? message.model : message.modelName);
    const std::vector<std::string> siblings = chat.siblings(id);
    if (siblings.size() > 1) {
      const auto pos = std::find(siblings.begin(), siblings.end(), id);
      who += "  " + std::to_string(std::distance(siblings.begin(), pos) + 1) + "/" + std::to_string(siblings.size());
    }
    if (!message.error.empty()) who += "  !";
    const float textLeft = min.x + 7.0f * z;
    const float clipRight = max.x - 5.0f * z;
    dl->PushClipRect(min, max, true);
    dl->AddText(ImVec2(textLeft, min.y + 4.0f * z), live ? palette.textDim : palette.textFaint, who.c_str());
    const std::string words = excerpt(message, 60);
    ImGui::RenderTextEllipsis(dl, ImVec2(textLeft, min.y + 4.0f * z + lineH), ImVec2(clipRight, max.y),
                              clipRight, words.c_str(), nullptr, nullptr);
    dl->PopClipRect();

    if (hovered) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(360.0f);
      theme::pushBold();
      ImGui::TextUnformatted(who.c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::dimText("%s", core::relativeTime(message.timestamp).c_str());
      ImGui::TextUnformatted(excerpt(message, 400).c_str());
      if (!live) widgets::dimText("Click to switch the transcript to this branch");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    // Keep the current node in view when the branch changes or on request.
    if (current && (gScrollToCurrent || gLastCurrent != id)) {
      ImGui::SetScrollFromPosX(c.x - ImGui::GetWindowPos().x, 0.5f);
      ImGui::SetScrollFromPosY(c.y - ImGui::GetWindowPos().y, 0.5f);
      gScrollToCurrent = false;
      gLastCurrent = id;
    }
  }
  ImGui::PopFont();
  // Reserve the canvas extent so the scrollbars cover every node.
  ImGui::SetCursorScreenPos(origin);
  ImGui::Dummy(canvasSize);

  // Drag the empty canvas to pan.
  if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
    ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::EndChild();

  if (!pruneTarget.empty()) pruneSubtree(pruneTarget);
}

}  // namespace app
