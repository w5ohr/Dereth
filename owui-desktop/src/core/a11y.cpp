#include "a11y.h"

#include <algorithm>
#include <cstdio>
#include <functional>

namespace a11y {

const char* roleName(Role role) {
  switch (role) {
    case Role::Group: return "group";
    case Role::Button: return "button";
    case Role::CheckBox: return "checkbox";
    case Role::Tab: return "tab";
    case Role::MenuItem: return "menuitem";
    case Role::PopUpButton: return "popup";
    case Role::Disclosure: return "disclosure";
    case Role::TextField: return "textfield";
    case Role::StaticText: return "text";
    case Role::Heading: return "heading";
    case Role::Dialog: return "dialog";
  }
  return "?";
}

Rect Rect::unite(const Rect& o) const {
  if (empty()) return o;
  if (o.empty()) return *this;
  const float x0 = std::min(x, o.x), y0 = std::min(y, o.y);
  const float x1 = std::max(x + w, o.x + o.w), y1 = std::max(y + h, o.y + o.h);
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

// --- Snapshot ---------------------------------------------------------------

void Snapshot::rebuildIndex() {
  byKey_.clear();
  for (size_t i = 0; i < nodes.size(); ++i) byKey_.emplace(nodes[i].key, static_cast<int>(i));
}

int Snapshot::indexOf(uint64_t key) const {
  auto it = byKey_.find(key);
  return it == byKey_.end() ? -1 : it->second;
}

const Node* Snapshot::find(uint64_t key) const {
  const int i = indexOf(key);
  return i < 0 ? nullptr : &nodes[static_cast<size_t>(i)];
}

int Snapshot::hitTest(float x, float y) const {
  // Depth first, last drawn wins among siblings (it is on top).
  int best = -1, bestDepth = -1;
  std::function<void(int, int)> walk;
  walk = [&](int index, int depth) {
    const Node& n = nodes[static_cast<size_t>(index)];
    const bool inside = n.frame.contains(x, y);
    if (inside && depth >= bestDepth) {
      best = index;
      bestDepth = depth;
    }
    // Groups without a frame still hold children that may be hit.
    if (inside || n.frame.empty()) {
      for (int child : n.children) walk(child, depth + 1);
    }
  };
  for (int root : roots) walk(root, 0);
  return best;
}

int Snapshot::focusedIndex() const {
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].focused) return static_cast<int>(i);
  }
  return -1;
}

uint64_t Snapshot::structureHash() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  for (const Node& n : nodes) {
    mix(n.key);
    mix(static_cast<uint64_t>(n.parent + 1));
    mix(static_cast<uint64_t>(n.role));
  }
  return h;
}

std::string Snapshot::describe() const {
  std::string out;
  std::function<void(int, int)> walk = [&](int index, int depth) {
    const Node& n = nodes[static_cast<size_t>(index)];
    out.append(static_cast<size_t>(depth) * 2, ' ');
    out += roleName(n.role);
    out += " \"" + n.label + "\"";
    if (!n.value.empty()) {
      std::string v = n.value.size() > 60 ? n.value.substr(0, 60) + "..." : n.value;
      std::replace(v.begin(), v.end(), '\n', ' ');
      out += " = \"" + v + "\"";
    }
    if (n.checkable) out += n.checked ? " [on]" : " [off]";
    if (n.expandable) out += n.expanded ? " [open]" : " [closed]";
    if (n.selected) out += " [selected]";
    if (n.focused) out += " [focused]";
    if (!n.enabled) out += " [disabled]";
    char buf[64];
    std::snprintf(buf, sizeof buf, " @%.0f,%.0f %.0fx%.0f", n.frame.x, n.frame.y, n.frame.w, n.frame.h);
    out += buf;
    out += "\n";
    for (int child : n.children) walk(child, depth + 1);
  };
  for (int root : roots) walk(root, 0);
  return out;
}

// --- Builder ----------------------------------------------------------------

void Builder::begin() {
  nodes_.clear();
  stack_.clear();
  seen_.clear();
  last_ = -1;
}

int Builder::add(Node node) {
  // Two items with one ImGui ID (or two panes' keys colliding) must still be
  // two elements; the second gets a derived key that is stable while the
  // order is.
  auto [it, fresh] = seen_.emplace(node.key, 1);
  if (!fresh) node.key = keyFor(std::to_string(it->second++), node.key);
  node.parent = stack_.empty() ? -1 : stack_.back();
  node.children.clear();
  const int index = static_cast<int>(nodes_.size());
  nodes_.push_back(std::move(node));
  if (nodes_.back().parent >= 0) nodes_[static_cast<size_t>(nodes_.back().parent)].children.push_back(index);
  last_ = index;
  return index;
}

int Builder::beginGroup(Node node) {
  const int index = add(std::move(node));
  stack_.push_back(index);
  return index;
}

void Builder::endGroup(const Rect& frame) {
  if (stack_.empty()) return;
  const int index = stack_.back();
  stack_.pop_back();
  Node& group = nodes_[static_cast<size_t>(index)];
  if (!frame.empty()) {
    group.frame = frame;
  } else if (group.frame.empty()) {
    Rect united;
    for (int child : group.children) united = united.unite(nodes_[static_cast<size_t>(child)].frame);
    group.frame = united;
  }
  last_ = index;
}

void Builder::endGroupsTo(int depth) {
  while (static_cast<int>(stack_.size()) > std::max(0, depth)) endGroup();
}

Snapshot Builder::finish(uint32_t modalScope, const std::string& modalTitle) {
  endGroupsTo(0);
  Snapshot snap;
  if (modalScope == 0) {
    snap.nodes = std::move(nodes_);
    for (size_t i = 0; i < snap.nodes.size(); ++i) {
      if (snap.nodes[i].parent < 0) snap.roots.push_back(static_cast<int>(i));
    }
  } else {
    // Keep the modal's subtrees only: nodes drawn in its scope whose parent
    // was not, re-parented under one Dialog root.
    std::vector<int> remap(nodes_.size(), -1);
    Node dialog;
    dialog.key = keyFor("dialog", modalScope);
    dialog.role = Role::Dialog;
    dialog.label = modalTitle;
    snap.nodes.push_back(dialog);
    snap.roots.push_back(0);
    for (size_t i = 0; i < nodes_.size(); ++i) {
      const Node& n = nodes_[i];
      const bool inScope = n.scope == modalScope;
      const bool parentKept = n.parent >= 0 && remap[static_cast<size_t>(n.parent)] >= 0;
      if (!inScope && !parentKept) continue;
      Node copy = n;
      copy.children.clear();
      copy.parent = parentKept ? remap[static_cast<size_t>(n.parent)] : 0;
      remap[i] = static_cast<int>(snap.nodes.size());
      snap.nodes.push_back(std::move(copy));
      snap.nodes[static_cast<size_t>(snap.nodes.back().parent)].children.push_back(remap[i]);
    }
    Rect united;
    for (int child : snap.nodes[0].children) united = united.unite(snap.nodes[static_cast<size_t>(child)].frame);
    snap.nodes[0].frame = united;
    nodes_.clear();
  }
  snap.rebuildIndex();
  stack_.clear();
  last_ = -1;
  return snap;
}

// --- Text -------------------------------------------------------------------

std::string cleanLabel(std::string_view raw) {
  // "###" keeps nothing after it in the label; "##" hides the rest.
  size_t cut = raw.find("##");
  std::string_view visible = cut == std::string_view::npos ? raw : raw.substr(0, cut);
  std::string out;
  out.reserve(visible.size());
  bool space = false;
  for (size_t i = 0; i < visible.size();) {
    const unsigned char c = static_cast<unsigned char>(visible[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (i + len > visible.size()) len = visible.size() - i;
    uint32_t cp = c;
    if (len == 2) cp = ((c & 0x1F) << 6) | (visible[i + 1] & 0x3F);
    else if (len == 3) cp = ((c & 0x0F) << 12) | ((visible[i + 1] & 0x3F) << 6) | (visible[i + 2] & 0x3F);
    else if (len == 4) cp = ((c & 0x07) << 18) | ((visible[i + 1] & 0x3F) << 12) | ((visible[i + 2] & 0x3F) << 6) | (visible[i + 3] & 0x3F);
    const bool privateUse = (cp >= 0xE000 && cp <= 0xF8FF) || cp >= 0xF0000;
    const bool control = cp < 0x20 || cp == 0x7F;
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == 0xA0) {
      space = !out.empty();
    } else if (!privateUse && !control) {
      if (space) out += ' ';
      space = false;
      out.append(visible.substr(i, len));
    }
    i += len;
  }
  return out;
}

uint64_t keyFor(std::string_view text, uint64_t seed) {
  uint64_t h = 1469598103934665603ull ^ (seed * 0x9E3779B97F4A7C15ull);
  for (unsigned char c : text) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h ? h : 1;
}

std::string messageLabel(const std::string& author, const std::string& when, bool streaming, bool pinned) {
  std::string out = author.empty() ? "Message" : author;
  if (!when.empty()) out += ", " + when;
  if (pinned) out += ", pinned";
  if (streaming) out += ", writing";
  return out;
}

std::string replyAnnouncement(const std::string& model, const std::string& plainText, size_t maxChars) {
  std::string text = cleanLabel(plainText);   // one line, no stray glyphs
  if (text.size() > maxChars) {
    size_t cut = text.rfind(' ', maxChars);
    if (cut == std::string::npos || cut < maxChars / 2) cut = maxChars;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;   // not mid-character
    text = text.substr(0, cut) + "...";
  }
  const std::string who = model.empty() ? "The assistant" : model;
  return text.empty() ? who + " finished." : who + " replied: " + text;
}

const char* regionName(Region region) {
  switch (region) {
    case Region::Rail: return "Navigation";
    case Region::Sidebar: return "Chat list";
    case Region::Main: return "Conversation";
    case Region::Composer: return "Message field";
    case Region::None: break;
  }
  return "";
}

Region nextRegion(Region current, const std::vector<Region>& available, int direction) {
  if (available.empty()) return Region::None;
  const int n = static_cast<int>(available.size());
  auto it = std::find(available.begin(), available.end(), current);
  if (it == available.end()) return direction >= 0 ? available.front() : available.back();
  const int index = static_cast<int>(it - available.begin());
  return available[static_cast<size_t>(((index + (direction >= 0 ? 1 : -1)) % n + n) % n)];
}

}  // namespace a11y
