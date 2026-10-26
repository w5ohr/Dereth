#include "ui/a11y_imgui.h"

#include <mutex>
#include <unordered_map>

#include "imgui_internal.h"

namespace a11y::ui {
namespace {

struct Added {
  ImRect bb;
  ImGuiItemFlags itemFlags = 0;
  bool visible = false;
  bool window = false;   // a window registering itself, not a widget
  uint64_t frame = 0;
};

// Tab items report their label before their rectangle: held until ItemAdd.
struct PendingInfo {
  std::string label;
  ImGuiItemStatusFlags flags = 0;
  uint64_t frame = 0;
};

struct Press {
  uint64_t key = 0;
  int stage = 0;
};

// One per ImGui context: each window (#121) has its own tree.
struct State {
  bool collecting = false;
  uint64_t frame = 0;
  Builder builder;
  Snapshot snap;
  std::unordered_map<ImGuiID, Added> added;
  std::unordered_map<ImGuiID, PendingInfo> pendingInfo;
  std::unordered_map<int, int> textOrdinal;   // parent index -> static texts so far
  bool lastSkipped = false;                   // the last hooked item was clipped: don't name the previous one
  Press press;
  uint64_t lastPressed = 0;
};
std::unordered_map<ImGuiContext*, State> gStates;
State& stateFor(ImGuiContext* ctx) { return gStates[ctx]; }
State& S() { return stateFor(GImGui); }
#define gCollecting (S().collecting)
#define gFrame (S().frame)
#define gBuilder (S().builder)
#define gSnap (S().snap)
#define gAdded (S().added)
#define gPendingInfo (S().pendingInfo)
#define gTextOrdinal (S().textOrdinal)
#define gLastSkipped (S().lastSkipped)
#define gPress (S().press)
#define gLastPressed (S().lastPressed)

std::mutex gAnnounceMutex;
std::vector<std::string> gAnnouncements;
std::function<void(const std::string&)> gFocusHandler;

uint32_t scopeOf(ImGuiWindow* window) {
  return window && window->RootWindow ? window->RootWindow->ID : 0;
}

Rect toRect(const ImRect& r) { return Rect{r.Min.x, r.Min.y, r.Max.x - r.Min.x, r.Max.y - r.Min.y}; }

bool inMenuPopup(ImGuiWindow* window) {
  for (ImGuiWindow* w = window; w; w = w->ParentWindow) {
    if (w->Flags & ImGuiWindowFlags_Modal) return false;
    if (w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu)) return !(w->Flags & ImGuiWindowFlags_Tooltip);
    if (!(w->Flags & ImGuiWindowFlags_ChildWindow)) return false;
  }
  return false;
}

void onItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
  auto it = gAdded.find(id);
  if (it == gAdded.end() || it->second.frame != gFrame) {
    gPendingInfo[id] = PendingInfo{label ? label : "", flags, gFrame};
    return;
  }
  if (!it->second.visible || it->second.window) {
    gLastSkipped = true;
    return;
  }
  gLastSkipped = false;
  ImGuiWindow* window = ctx->CurrentWindow;
  Node node;
  node.key = id;
  node.label = cleanLabel(label ? label : "");
  node.frame = toRect(it->second.bb);
  node.enabled = !(it->second.itemFlags & ImGuiItemFlags_Disabled);
  node.scope = scopeOf(window);
  node.window = window ? window->ID : 0;
  node.focused = ctx->NavId == id && ctx->NavCursorVisible && ctx->NavWindow == window;
  const bool checkable = (flags & ImGuiItemStatusFlags_Checkable) != 0;
  const bool openable = (flags & ImGuiItemStatusFlags_Openable) != 0;
  const bool inputable = (flags & ImGuiItemStatusFlags_Inputable) != 0;
  ImGuiTabBar* tabBar = ctx->CurrentTabBar;
  if (tabBar && ImGui::TabBarFindTabByID(tabBar, id)) {
    node.role = Role::Tab;
    node.selected = tabBar->VisibleTabId == id;
  } else if (inputable) {
    node.role = Role::TextField;
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(id)) {
      if (!(state->Flags & ImGuiInputTextFlags_Password) && state->TextSrc) node.value.assign(state->TextSrc, static_cast<size_t>(state->TextLen));
    }
  } else if (inMenuPopup(window)) {
    node.role = Role::MenuItem;
    node.checkable = checkable;
    node.checked = (flags & ImGuiItemStatusFlags_Checked) != 0;
    node.expandable = openable;
  } else if (checkable) {
    node.role = Role::CheckBox;
    node.checkable = true;
    node.checked = (flags & ImGuiItemStatusFlags_Checked) != 0;
  } else if (openable) {
    node.role = Role::Disclosure;
    node.expandable = true;
    node.expanded = (flags & ImGuiItemStatusFlags_Opened) != 0;
  } else {
    node.role = Role::Button;
  }
  gBuilder.add(std::move(node));
}

}  // namespace

bool gListeningFlag = false;
void setListening(bool on) { gListeningFlag = on; }
bool listening() { return gListeningFlag; }

void install(ImGuiContext* context) {
  if (context) context->TestEngineHookItems = true;
}

void forget(ImGuiContext* context) { gStates.erase(context); }

const Snapshot& snapshotFor(ImGuiContext* context) { return stateFor(context).snap; }

void requestPress(ImGuiContext* context, uint64_t key) {
  State& st = stateFor(context);
  st.press.key = key;
  st.press.stage = 0;
}

void beginFrame() {
  ++gFrame;
  gCollecting = true;
  gBuilder.begin();
  gTextOrdinal.clear();
  gLastSkipped = false;
  if (gAdded.size() > 8192) gAdded.clear();
  gPendingInfo.clear();
}

void endFrame() {
  if (!gCollecting) return;
  gCollecting = false;
  ImGuiContext& g = *GImGui;
  ImGuiWindow* modal = ImGui::GetTopMostPopupModal();
  Snapshot snap = gBuilder.finish(modal ? scopeOf(modal) : 0, modal ? cleanLabel(modal->Name) : "");
  // Keyboard focus: ImGui's nav cursor when it is showing, else whatever the
  // panes marked (the focused message).
  bool navFocus = false;
  for (const Node& n : snap.nodes) navFocus |= n.focused && n.key == g.NavId;
  if (navFocus) {
    for (Node& n : snap.nodes) n.focused = n.key == g.NavId;
  }
  gSnap = std::move(snap);
  // Forget hook records from frames ago.
  for (auto it = gAdded.begin(); it != gAdded.end();) {
    if (gFrame - it->second.frame > 2) it = gAdded.erase(it);
    else ++it;
  }
}

const Snapshot& snapshot() { return gSnap; }
uint64_t frameNumber() { return gFrame; }

void nameLastItem(const std::string& label, bool replace) {
  if (!gCollecting || gLastSkipped) return;
  Node* node = gBuilder.lastNode();
  if (!node || node->key != ImGui::GetItemID()) return;   // the last item was not recorded
  if (replace || node->label.empty()) node->label = cleanLabel(label);
}

void markLastItem(bool selected) {
  if (!gCollecting || gLastSkipped) return;
  Node* node = gBuilder.lastNode();
  if (!node || node->key != ImGui::GetItemID()) return;
  node->role = Role::Tab;
  node->selected = selected;
}

void helpLastItem(const std::string& help) {
  if (!gCollecting || gLastSkipped) return;
  Node* node = gBuilder.lastNode();
  if (!node || node->key != ImGui::GetItemID()) return;
  if (node->help.empty()) node->help = help;
}

int beginGroup(std::string_view key, Role role, const std::string& label, const std::string& value, const std::string& tag) {
  if (!gCollecting) return -1;
  ImGuiWindow* window = GImGui->CurrentWindow;
  Node node;
  node.key = keyFor(key, scopeOf(window));
  node.role = role;
  node.label = label;
  node.value = value;
  node.tag = tag;
  node.scope = scopeOf(window);
  return gBuilder.beginGroup(std::move(node));
}

void endGroup(ImVec2 min, ImVec2 max) {
  if (!gCollecting) return;
  Rect frame{min.x, min.y, max.x - min.x, max.y - min.y};
  gBuilder.endGroup(frame);
}

Group::Group(std::string_view key, Role role, const std::string& label, const std::string& value, const std::string& tag) {
  if (!gCollecting) return;
  depth_ = gBuilder.depth();
  index_ = beginGroup(key, role, label, value, tag);
  open_ = index_ >= 0;
}

Group::~Group() {
  if (open_ && gCollecting) gBuilder.endGroupsTo(depth_);
}

void Group::close(ImVec2 min, ImVec2 max) {
  if (!open_ || !gCollecting) return;
  gBuilder.endGroupsTo(depth_ + 1);   // anything a pane left open inside
  gBuilder.endGroup(Rect{min.x, min.y, max.x - min.x, max.y - min.y});
  open_ = false;
}

void Group::setFocused(bool focused) {
  if (Node* node = gBuilder.at(index_)) node->focused = focused;
}

void staticText(const std::string& label, const std::string& value, ImVec2 min, ImVec2 max, bool evenIfHidden) {
  if (!gCollecting || (label.empty() && value.empty())) return;
  ImGuiWindow* window = GImGui->CurrentWindow;
  if (!window || window->SkipItems) return;
  const ImRect bb(min, max);
  if (!evenIfHidden && !window->ClipRect.Overlaps(bb) && max.y > min.y) return;   // scrolled out of sight
  // Key by position among the static texts of the same group, so a text
  // whose words change (a timer, a streamed reply) keeps its element.
  const int group = gBuilder.currentGroup();
  const Node* holder = gBuilder.at(group);
  const int ordinal = gTextOrdinal[group]++;
  Node node;
  node.key = keyFor("text#" + std::to_string(ordinal), holder ? holder->key : scopeOf(window));
  node.role = Role::StaticText;
  node.label = label;
  node.value = value;
  node.frame = Rect{min.x, min.y, max.x - min.x, max.y - min.y};
  node.scope = scopeOf(window);
  gBuilder.add(std::move(node));
}

void textItem(const std::string& text) {
  if (!gCollecting) return;
  const std::string clean = cleanLabel(text);
  if (clean.empty()) return;
  staticText("", clean, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

void headingItem(const std::string& text) {
  if (!gCollecting) return;
  const std::string clean = cleanLabel(text);
  if (clean.empty()) return;
  ImGuiWindow* window = GImGui->CurrentWindow;
  if (!window || window->SkipItems) return;
  const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
  if (!window->ClipRect.Overlaps(ImRect(min, max))) return;
  Node node;
  node.key = keyFor("heading:" + clean, scopeOf(window));
  node.role = Role::Heading;
  node.label = clean;
  node.frame = Rect{min.x, min.y, max.x - min.x, max.y - min.y};
  node.scope = scopeOf(window);
  gBuilder.add(std::move(node));
}

void linkItem(const std::string& text) {
  if (!gCollecting) return;
  const std::string clean = cleanLabel(text);
  ImGuiWindow* window = GImGui->CurrentWindow;
  if (clean.empty() || !window || window->SkipItems) return;
  const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
  if (!window->ClipRect.Overlaps(ImRect(min, max))) return;
  Node node;
  node.key = keyFor("link:" + clean, scopeOf(window));
  node.role = Role::Button;
  node.label = clean;
  node.frame = Rect{min.x, min.y, max.x - min.x, max.y - min.y};
  node.scope = scopeOf(window);
  gBuilder.add(std::move(node));
}

uint64_t groupKey(std::string_view key) {
  ImGuiWindow* root = GImGui ? ImGui::FindWindowByName("##root") : nullptr;
  return keyFor(key, root ? root->ID : 0);
}

bool focusFirstIn(std::string_view key) {
  const int start = gSnap.indexOf(groupKey(key));
  if (start < 0) return false;
  // Depth first through the group for the first control ImGui knows by ID.
  std::vector<int> stack{start};
  while (!stack.empty()) {
    const int index = stack.back();
    stack.pop_back();
    const Node& n = gSnap.nodes[static_cast<size_t>(index)];
    const bool control = n.role != Role::Group && n.role != Role::StaticText && n.role != Role::Heading && n.role != Role::Dialog;
    if (control && n.enabled && n.window != 0) {
      ImGuiWindow* window = ImGui::FindWindowByID(n.window);
      if (window) {
        ImGui::FocusWindow(window);
        ImGui::SetFocusID(static_cast<ImGuiID>(n.key), window);
        ImGui::SetNavCursorVisible(true);
        GImGui->NavHighlightItemUnderNav = true;
        return true;
      }
    }
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) stack.push_back(*it);
  }
  return false;
}

void announce(const std::string& text) {
  if (text.empty()) return;
  std::lock_guard<std::mutex> lock(gAnnounceMutex);
  gAnnouncements.push_back(text);
  if (gAnnouncements.size() > 20) gAnnouncements.erase(gAnnouncements.begin());
}

std::vector<std::string> takeAnnouncements() {
  std::lock_guard<std::mutex> lock(gAnnounceMutex);
  std::vector<std::string> out;
  out.swap(gAnnouncements);
  return out;
}

void requestPress(uint64_t key) {
  gPress.key = key;
  gPress.stage = 0;
}

bool pressInFlight() { return gPress.key != 0; }
uint64_t lastPressedKey() { return gLastPressed; }

void pumpInput(ImGuiIO& io) {
  if (!gPress.key) return;
  const Node* node = gSnap.find(gPress.key);
  if (!node || node->frame.empty()) {
    gPress = Press{};
    return;
  }
  const float x = node->frame.x + node->frame.w * 0.5f;
  const float y = node->frame.y + node->frame.h * 0.5f;
  io.AddMousePosEvent(x, y);
  // Pointer first, then down, then up: one change per frame, so ImGui sees
  // the hover before the press and the press before the release.
  if (gPress.stage == 1) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  if (gPress.stage == 2) {
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    gLastPressed = gPress.key;
    gPress = Press{};
    return;
  }
  ++gPress.stage;
}

void setFocusHandler(std::function<void(const std::string& tag)> handler) { gFocusHandler = std::move(handler); }

void requestFocus(uint64_t key) {
  const Node* node = gSnap.find(key);
  if (node && !node->tag.empty() && gFocusHandler) gFocusHandler(node->tag);
}

}  // namespace a11y::ui

// --- ImGui's test-engine hooks (IMGUI_ENABLE_TEST_ENGINE) -------------------

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data) {
  using namespace a11y::ui;
  if (!a11y::ui::stateFor(ctx).collecting || id == 0) return;
  ImGuiWindow* window = ctx->CurrentWindow;
  Added& added = gAdded[id];
  added.bb = bb;
  added.itemFlags = item_data ? item_data->ItemFlags : 0;
  added.visible = window && !window->SkipItems && !window->Hidden && window->ClipRect.Overlaps(bb);
  added.frame = gFrame;
  added.window = item_data == nullptr;
  auto pending = gPendingInfo.find(id);
  if (pending != gPendingInfo.end()) {
    const PendingInfo info = pending->second;
    gPendingInfo.erase(pending);
    if (info.frame == gFrame) onItemInfo(ctx, id, info.label.c_str(), info.flags);
  }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
  if (!a11y::ui::stateFor(ctx).collecting) return;
  a11y::ui::onItemInfo(ctx, id, label, flags);
}

void ImGuiTestEngineHook_ComboInfo(ImGuiContext* ctx, ImGuiID id, const char* label, const char* preview, bool open) {
  using namespace a11y::ui;
  if (!gCollecting) return;
  const int before = gBuilder.size();
  onItemInfo(ctx, id, label, ImGuiItemStatusFlags_Openable | (open ? ImGuiItemStatusFlags_Opened : 0));
  if (gBuilder.size() == before) return;   // clipped
  a11y::Node* node = gBuilder.lastNode();
  node->role = a11y::Role::PopUpButton;
  node->value = preview ? a11y::cleanLabel(preview) : "";
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }
