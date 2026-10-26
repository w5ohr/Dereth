// `Open WebUI --voiceovertest` (#142): the accessibility tree end to end without
// VoiceOver - the tree logic, the real ImGui item hooks in a headless
// context, and the Cocoa elements an assistive app reads, in an offscreen
// window.
#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <string>

#include "core/a11y.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/a11y_bridge.h"
#include "ui/a11y_imgui.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

const a11y::Node* byLabel(const a11y::Snapshot& snap, const std::string& label) {
  for (const a11y::Node& n : snap.nodes) {
    if (n.label == label) return &n;
  }
  return nullptr;
}

std::string str(id value) {
  if (!value) return "";
  if ([value isKindOfClass:[NSString class]]) return [(NSString*)value UTF8String];
  if ([value isKindOfClass:[NSNumber class]]) return [[(NSNumber*)value stringValue] UTF8String];
  return [[value description] UTF8String];
}

void testCore() {
  using namespace a11y;
  report("cleanLabel hides ## ids, keeps ### titles, drops icon glyphs and collapses spaces",
         cleanLabel("Save##btn") == "Save" && cleanLabel("##nav").empty() && cleanLabel("Title###win") == "Title" &&
             cleanLabel("\xEE\x80\x81  Copy \n  code ") == "Copy code" && cleanLabel("caf\xC3\xA9") == "caf\xC3\xA9",
         "'" + cleanLabel("\xEE\x80\x81  Copy \n  code ") + "'");

  Builder b;
  b.begin();
  Node region;
  region.key = keyFor("region");
  region.label = "Sidebar";
  b.beginGroup(region);
  Node one;
  one.key = 42;
  one.role = Role::Button;
  one.label = "New chat";
  one.frame = a11y::Rect{10, 10, 100, 30};
  b.add(one);
  Node two = one;
  two.label = "Same ID twice";
  two.frame = a11y::Rect{10, 50, 100, 30};
  b.add(two);
  b.endGroup();
  Snapshot s = b.finish();
  const Node* g = s.find(keyFor("region"));
  report("groups hold what was drawn inside and take the union of their children's frames",
         s.roots.size() == 1 && g && g->children.size() == 2 && g->frame.x == 10 && g->frame.y == 10 && g->frame.h == 70);
  const uint64_t secondKey = s.nodes[2].key;
  b.begin();
  b.beginGroup(region);
  b.add(one);
  b.add(two);
  b.endGroup();
  Snapshot again = b.finish();
  report("a repeated ID becomes a second node whose key is stable from frame to frame",
         s.nodes[1].key == 42 && secondKey != 42 && again.nodes[2].key == secondKey);
  report("hit testing finds the deepest node under the point", s.hitTest(20, 60) == 2 && s.hitTest(500, 500) == -1);
  Snapshot changed = again;
  changed.nodes[1].label = "Renamed";
  report("the structure hash ignores text changes and notices nodes coming and going",
         changed.structureHash() == again.structureHash() && again.structureHash() != Snapshot{}.structureHash());

  // A modal: only its subtree survives, under one Dialog root.
  b.begin();
  Node behind = one;
  behind.scope = 1;
  b.add(behind);
  Node inModal = one;
  inModal.key = 7;
  inModal.label = "Delete";
  inModal.scope = 2;
  b.add(inModal);
  Snapshot modal = b.finish(2, "Delete chat?");
  report("an open modal hides everything drawn outside it",
         modal.roots.size() == 1 && modal.nodes[0].role == Role::Dialog && modal.nodes[0].label == "Delete chat?" &&
             modal.nodes[0].children.size() == 1 && modal.find(7) && !modal.find(42));

  report("messageLabel says who, when, pinned and still writing",
         messageLabel("qwen3:8b", "2 minutes ago", true, true) == "qwen3:8b, 2 minutes ago, pinned, writing" &&
             messageLabel("", "", false, false) == "Message");
  const std::string longReply = "The lighthouse keeper watched the storm roll in over the cliffs while the lamp turned, "
                                "and every wave that broke on the rocks sounded like a door slamming somewhere far below the tower.";
  const std::string spoken = replyAnnouncement("qwen3:8b", longReply, 80);
  report("replyAnnouncement names the model and cuts at a word", spoken.rfind("qwen3:8b replied: The lighthouse", 0) == 0 &&
                                                                       spoken.size() < 120 && spoken.substr(spoken.size() - 3) == "..." &&
                                                                       spoken[spoken.size() - 4] != ' ' && replyAnnouncement("", "") == "The assistant finished.",
         spoken);
  const std::vector<Region> all{Region::Rail, Region::Sidebar, Region::Main, Region::Composer};
  report("regions cycle forwards and backwards and wrap",
         nextRegion(Region::None, all, 1) == Region::Rail && nextRegion(Region::Composer, all, 1) == Region::Rail &&
             nextRegion(Region::Rail, all, -1) == Region::Composer && nextRegion(Region::Sidebar, {Region::Rail, Region::Main}, 1) == Region::Rail &&
             nextRegion(Region::Rail, {}, 1) == Region::None);
}

// One headless frame drawing `draw` inside a full-screen "##root" window.
template <typename Draw>
void frame(Draw&& draw) {
  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = 1.0f / 60.0f;
  a11y::ui::pumpInput(io);
  ImGui::NewFrame();
  a11y::ui::beginFrame();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(800, 600));
  ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
  draw();
  ImGui::End();
  a11y::ui::endFrame();
  ImGui::Render();
}

bool gDark = true;
int gSavePresses = 0;
bool gShowModal = false;
uint64_t gFocusTarget = 0;
bool gFocusRequested = false;

void drawUi() {
  if (gFocusRequested) {
    gFocusRequested = false;
    a11y::ui::focusFirstIn("region:tools");
  }
  ImGui::TextUnformatted("Settings");
  a11y::ui::headingItem("Settings");
  if (ImGui::Button("Save##primary")) ++gSavePresses;
  ImGui::Checkbox("Dark mode", &gDark);
  ImGui::InvisibleButton("##copyicon", ImVec2(24, 24));
  a11y::ui::nameLastItem("Copy");
  static char query[64] = "";
  ImGui::InputText("##query", query, sizeof query);
  a11y::ui::nameLastItem("Search chats");
  if (ImGui::TreeNode("Advanced")) ImGui::TreePop();
  static int language = 1;
  const char* languages[] = {"English", "Deutsch", "Fran\xC3\xA7" "ais"};
  ImGui::Combo("Language", &language, languages, 3);
  if (ImGui::BeginTabBar("tabs")) {
    if (ImGui::BeginTabItem("General")) ImGui::EndTabItem();
    if (ImGui::BeginTabItem("Network")) ImGui::EndTabItem();
    ImGui::EndTabBar();
  }
  {
    a11y::ui::Group group("region:tools", a11y::Role::Group, "Tools");
    ImGui::Button("Run tool");
    ImGui::Button("Stop tool");
    a11y::ui::staticText("", "Plain words of a message", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    // No close(): the scope closes it, as an early `continue` would.
  }
  ImGui::Button("After the group");
  if (gShowModal) ImGui::OpenPopup("Delete chat?");
  if (ImGui::BeginPopupModal("Delete chat?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Button("Delete");
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void testImGui() {
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(800, 600);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.Fonts->AddFontDefault();
  a11y::ui::install(ImGui::GetCurrentContext());
  report("the item hooks are compiled in and switched on", ImGui::GetCurrentContext()->TestEngineHookItems);

  frame(drawUi);
  frame(drawUi);
  const a11y::Snapshot& snap = a11y::ui::snapshot();
  const a11y::Node* save = byLabel(snap, "Save");
  const a11y::Node* dark = byLabel(snap, "Dark mode");
  const a11y::Node* copy = byLabel(snap, "Copy");
  const a11y::Node* search = byLabel(snap, "Search chats");
  const a11y::Node* advanced = byLabel(snap, "Advanced");
  const a11y::Node* general = byLabel(snap, "General");
  const a11y::Node* network = byLabel(snap, "Network");
  const a11y::Node* heading = byLabel(snap, "Settings");
  const a11y::Node* combo = byLabel(snap, "Language");
  report("a button is a button with its visible label and a real frame",
         save && save->role == a11y::Role::Button && save->frame.w > 20 && save->frame.h > 10 && save->enabled);
  report("a check box reports checked", dark && dark->role == a11y::Role::CheckBox && dark->checkable && dark->checked);
  report("an icon-only button takes the name it is given", copy && copy->role == a11y::Role::Button);
  report("a text field is a text field, named", search && search->role == a11y::Role::TextField);
  report("a tree node is a closed disclosure", advanced && advanced->role == a11y::Role::Disclosure && advanced->expandable && !advanced->expanded);
  report("tab items are tabs and the open one is selected",
         general && network && general->role == a11y::Role::Tab && general->selected && !network->selected);
  report("a heading is a heading", heading && heading->role == a11y::Role::Heading);
  report("a combo is a pop-up button whose value is the current choice (vendored BeginCombo patch)",
         combo && combo->role == a11y::Role::PopUpButton && combo->value == "Deutsch" && !combo->expanded);
  const a11y::Node* tools = byLabel(snap, "Tools");
  const a11y::Node* after = byLabel(snap, "After the group");
  report("a group closed by scope holds its two buttons and text, and what follows is outside it",
         tools && tools->children.size() == 3 && after && after->parent != snap.indexOf(tools->key) &&
             snap.nodes[static_cast<size_t>(tools->children[2])].role == a11y::Role::StaticText &&
             snap.nodes[static_cast<size_t>(tools->children[2])].value == "Plain words of a message");

  // Press: three frames of pointer, down, up at the button's centre.
  const uint64_t saveKey = save ? save->key : 0;
  a11y::ui::requestPress(saveKey);
  for (int i = 0; i < 5; ++i) frame(drawUi);
  report("VoiceOver's press is played in as a click that ImGui's button sees once",
         gSavePresses == 1 && !a11y::ui::pressInFlight() && a11y::ui::lastPressedKey() == saveKey,
         std::to_string(gSavePresses) + " press(es)");

  // Region focus: the first control in the group gets ImGui's nav cursor.
  gFocusRequested = true;
  frame(drawUi);
  frame(drawUi);
  const a11y::Node* run = byLabel(a11y::ui::snapshot(), "Run tool");
  const int focused = a11y::ui::snapshot().focusedIndex();
  report("focusFirstIn puts keyboard focus on the group's first control and the tree says so",
         run && ImGui::GetCurrentContext()->NavId == static_cast<ImGuiID>(run->key) && focused >= 0 &&
             a11y::ui::snapshot().nodes[static_cast<size_t>(focused)].label == "Run tool");

  // A modal hides the rest.
  gShowModal = true;
  frame(drawUi);
  gShowModal = false;
  frame(drawUi);
  frame(drawUi);
  const a11y::Snapshot& withModal = a11y::ui::snapshot();
  report("with a modal open only the dialog and its buttons are exposed",
         withModal.roots.size() == 1 && withModal.nodes[0].role == a11y::Role::Dialog && withModal.nodes[0].label == "Delete chat?" &&
             byLabel(withModal, "Delete") && byLabel(withModal, "Cancel") && !byLabel(withModal, "Save"),
         std::to_string(withModal.nodes.size()) + " nodes");
}

void testBridge() {
  [NSApplication sharedApplication];
  NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 800, 600)
                                                 styleMask:NSWindowStyleMaskTitled
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  OWUIMetalView* view = [[OWUIMetalView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
  window.contentView = view;

  // Close the modal so the whole tree is back, then read it the way VoiceOver does.
  const auto pressCancel = [] {
    if (const a11y::Node* cancel = byLabel(a11y::ui::snapshot(), "Cancel")) a11y::ui::requestPress(cancel->key);
  };
  pressCancel();
  for (int i = 0; i < 5; ++i) frame(drawUi);
  const a11y::Snapshot& snap = a11y::ui::snapshot();
  NSArray* roots = [view accessibilityChildren];
  report("the view's children are the tree's roots", roots.count == snap.roots.size() && roots.count > 5,
         std::to_string(roots.count) + " roots");

  id saveElement = nil, darkElement = nil, toolsElement = nil, headingElement = nil, generalElement = nil;
  for (id element in roots) {
    const std::string label = str([element accessibilityLabel]);
    if (label == "Save") saveElement = element;
    if (label == "Dark mode") darkElement = element;
    if (label == "Tools") toolsElement = element;
    if (str([element accessibilityTitle]) == "Settings") headingElement = element;
    if (label == "General") generalElement = element;
  }
  report("a button element: AXButton, its label, enabled",
         saveElement && [[saveElement accessibilityRole] isEqualToString:NSAccessibilityButtonRole] && [saveElement isAccessibilityEnabled]);
  report("a check box element: AXCheckBox with value 1", darkElement && [[darkElement accessibilityRole] isEqualToString:NSAccessibilityCheckBoxRole] &&
                                                         str([darkElement accessibilityValue]) == "1");
  report("a heading element: AXHeading titled", headingElement && [[headingElement accessibilityRole] isEqualToString:@"AXHeading"]);
  report("a tab element: radio button with the tab subrole, selected",
         generalElement && [[generalElement accessibilitySubrole] isEqualToString:NSAccessibilityTabButtonSubrole] && [generalElement isAccessibilitySelected]);
  NSArray* inside = [toolsElement accessibilityChildren];
  report("a group element's children point back at it",
         toolsElement && inside.count == 3 && [inside[0] accessibilityParent] == toolsElement && [toolsElement accessibilityParent] == view);

  const a11y::Node* save = byLabel(snap, "Save");
  const NSRect screen = [saveElement accessibilityFrame];
  const NSRect expected = [window convertRectToScreen:[view convertRect:NSMakeRect(save->frame.x, 600 - save->frame.y - save->frame.h, save->frame.w, save->frame.h) toView:nil]];
  report("element frames are in screen coordinates (flipped from ImGui's top-left)",
         NSEqualRects(NSIntegralRect(screen), NSIntegralRect(expected)) && screen.size.width == save->frame.w,
         [NSStringFromRect(screen) UTF8String]);
  const NSPoint centre = NSMakePoint(NSMidX(screen), NSMidY(screen));
  report("hit testing a point over Save returns the Save element", [view accessibilityHitTest:centre] == saveElement);

  // The same widget is the same object next frame (VoiceOver's cursor survives redraws).
  frame(drawUi);
  id saveAgain = nil;
  for (id element in [view accessibilityChildren]) {
    if (str([element accessibilityLabel]) == "Save") saveAgain = element;
  }
  report("elements are reused across frames", saveAgain == saveElement);

  const int before = gSavePresses;
  const bool pressed = [saveElement accessibilityPerformPress];
  for (int i = 0; i < 5; ++i) frame(drawUi);
  report("accessibilityPerformPress clicks the ImGui button", pressed && gSavePresses == before + 1);

  const std::string dump = a11y::bridge::dumpWindow((__bridge void*)view);
  report("the window dump (public NSAccessibility calls) lists the controls",
         dump.find("AXButton \"Save\"") != std::string::npos && dump.find("AXCheckBox \"Dark mode\" = \"1\"") != std::string::npos &&
             dump.find("AXGroup \"Tools\"") != std::string::npos,
         std::to_string(dump.size()) + " chars");
  report("pressLabel finds an element by name", a11y::bridge::pressLabel((__bridge void*)view, "Stop tool") &&
                                                    !a11y::bridge::pressLabel((__bridge void*)view, "No such button"));
  for (int i = 0; i < 5; ++i) frame(drawUi);

  a11y::ui::announce("qwen3:8b replied: hello");
  a11y::ui::announce("Copied");
  const std::vector<std::string> queued = a11y::ui::takeAnnouncements();
  report("announcements queue in order and drain once",
         queued.size() == 2 && queued[0] == "qwen3:8b replied: hello" && a11y::ui::takeAnnouncements().empty());
  a11y::ui::announce("posted");
  a11y::bridge::frameDone((__bridge void*)view);
  report("frameDone posts and drains the queue", a11y::ui::takeAnnouncements().empty());
  ImGui::DestroyContext();
}
}  // namespace

int runVoiceOverSelfTest() {
  printf("Open WebUI desktop VoiceOver (accessibility tree) self-test\n\n");
  @autoreleasepool {
    testCore();
    testImGui();
    testBridge();
  }
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
