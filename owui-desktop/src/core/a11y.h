// Accessibility tree (#142): what VoiceOver sees of an immediate-mode UI.
//
// ImGui keeps no widget objects between frames, so the tree is rebuilt every
// frame from what was drawn: the item hooks report buttons, check boxes, text
// fields, menus and tabs, and the panes add headings, static text and
// message groups. A Snapshot is the finished tree of one frame; nodes keep
// the same key from frame to frame (the ImGui ID, or a hash of a pane's own
// key), so the native elements built on top of them survive redraws and
// VoiceOver's cursor stays put while a reply streams.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace a11y {

enum class Role {
  Group,        // a region or a message: label, children
  Button,
  CheckBox,     // checked
  Tab,          // selected (rail items, tab bars)
  MenuItem,     // checked when checkable
  PopUpButton,  // combo, menu that opens
  Disclosure,   // tree node, collapsing header: expanded
  TextField,    // value is the text when it is being edited
  StaticText,   // value is the text
  Heading,      // label is the text
  Dialog,       // a modal: only its contents are exposed while it is open
};

const char* roleName(Role role);

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;   // view points, origin top-left
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  Rect unite(const Rect& o) const;
};

struct Node {
  uint64_t key = 0;
  Role role = Role::Group;
  std::string label, value, help;
  Rect frame;
  bool enabled = true;
  bool checkable = false, checked = false;
  bool expandable = false, expanded = false;
  bool selected = false;
  bool focused = false;     // keyboard focus (ImGui's nav cursor, or the focused message)
  uint32_t scope = 0;       // the ImGui root window it was drawn in (modal filtering)
  uint32_t window = 0;      // the ImGui window of a control (keyboard focus moves)
  std::string tag;          // app meaning, e.g. "msg:<id>" (focus / scroll actions)
  int parent = -1;
  std::vector<int> children;
};

class Snapshot {
 public:
  std::vector<Node> nodes;
  std::vector<int> roots;

  const Node* find(uint64_t key) const;
  int indexOf(uint64_t key) const;
  int hitTest(float x, float y) const;        // deepest node whose frame holds the point; -1
  int focusedIndex() const;                   // -1 when nothing has keyboard focus
  uint64_t structureHash() const;             // changes when nodes come, go or move
  std::string describe() const;               // indented text dump (self-tests, debug)
  void rebuildIndex();

 private:
  std::unordered_map<uint64_t, int> byKey_;
};

// Collects one frame. Items go under the innermost open group.
class Builder {
 public:
  void begin();
  int add(Node node);                          // returns the index; a duplicate key is made unique
  int beginGroup(Node node);
  void endGroup(const Rect& frame = Rect{});   // empty frame: the union of the children
  int depth() const { return static_cast<int>(stack_.size()); }
  void endGroupsTo(int depth);
  int last() const { return last_; }
  int currentGroup() const { return stack_.empty() ? -1 : stack_.back(); }
  Node* lastNode() { return last_ >= 0 ? &nodes_[static_cast<size_t>(last_)] : nullptr; }
  Node* at(int index) { return index >= 0 && index < static_cast<int>(nodes_.size()) ? &nodes_[static_cast<size_t>(index)] : nullptr; }
  int size() const { return static_cast<int>(nodes_.size()); }
  // modalScope != 0: a modal is open; only what was drawn in it is exposed,
  // wrapped in a Dialog titled modalTitle.
  Snapshot finish(uint32_t modalScope = 0, const std::string& modalTitle = "");

 private:
  std::vector<Node> nodes_;
  std::vector<int> stack_;
  std::unordered_map<uint64_t, int> seen_;
  int last_ = -1;
};

// "Save##btn" -> "Save", "##id" -> "", "Title###id" -> "Title"; private-use
// icon glyphs and control characters are dropped and spaces collapsed.
std::string cleanLabel(std::string_view raw);

// FNV-1a; keys for nodes the panes add themselves.
uint64_t keyFor(std::string_view text, uint64_t seed = 0);

// "qwen3:8b, 2 minutes ago" / "You, just now, writing".
std::string messageLabel(const std::string& author, const std::string& when, bool streaming, bool pinned);

// What VoiceOver says when a reply finishes: the model and the start of it.
std::string replyAnnouncement(const std::string& model, const std::string& plainText, size_t maxChars = 160);

// Keyboard regions cycled by View > Next Region.
enum class Region { None, Rail, Sidebar, Main, Composer };
const char* regionName(Region region);
// The next of `available` after `current` in `direction` (+1/-1), wrapping;
// None when nothing is available.
Region nextRegion(Region current, const std::vector<Region>& available, int direction);

}  // namespace a11y
