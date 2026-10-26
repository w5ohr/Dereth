#import "ui/a11y_bridge.h"

#import <Cocoa/Cocoa.h>
#import <objc/message.h>

#include <cstdlib>
#include <fstream>
#include <unordered_set>

#include "core/a11y.h"
#include "native.h"
#include "ui/a11y_imgui.h"

namespace {
double gLastQuery = 0;   // when an assistive client last asked for our children
double nowSeconds() { return [NSProcessInfo processInfo].systemUptime; }
}

@class OWUIAXElement;

@interface OWUIMetalView ()
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, OWUIAXElement*>* elements;
@property(nonatomic) uint64_t lastHash;
@property(nonatomic) double lastLayoutPost;
@property(nonatomic) uint64_t lastFocusKey;
- (OWUIAXElement*)elementForKey:(uint64_t)key;
- (NSRect)screenRect:(const a11y::Rect&)r;
@end

@interface OWUIAXElement : NSAccessibilityElement
@property(nonatomic) uint64_t key;
@property(nonatomic, weak) OWUIMetalView* host;
@end

static const a11y::Snapshot& snapOf(OWUIMetalView* view) {
  return view && view.imguiContext ? a11y::ui::snapshotFor(static_cast<ImGuiContext*>(view.imguiContext)) : a11y::ui::snapshot();
}
static const a11y::Node* nodeFor(OWUIMetalView* host, uint64_t key) { return snapOf(host).find(key); }

@implementation OWUIAXElement

- (BOOL)isAccessibilityElement { return nodeFor(self.host, self.key) != nullptr; }

- (NSAccessibilityRole)accessibilityRole {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (!n) return NSAccessibilityUnknownRole;
  switch (n->role) {
    case a11y::Role::Group:
    case a11y::Role::Dialog: return NSAccessibilityGroupRole;
    case a11y::Role::Button: return NSAccessibilityButtonRole;
    case a11y::Role::CheckBox: return NSAccessibilityCheckBoxRole;
    case a11y::Role::Tab: return NSAccessibilityRadioButtonRole;
    case a11y::Role::MenuItem: return n->checkable ? NSAccessibilityCheckBoxRole : NSAccessibilityButtonRole;
    case a11y::Role::PopUpButton: return NSAccessibilityPopUpButtonRole;
    case a11y::Role::Disclosure: return NSAccessibilityDisclosureTriangleRole;
    case a11y::Role::TextField: return NSAccessibilityTextFieldRole;
    case a11y::Role::StaticText: return NSAccessibilityStaticTextRole;
    case a11y::Role::Heading: return @"AXHeading";
  }
  return NSAccessibilityUnknownRole;
}

- (NSAccessibilitySubrole)accessibilitySubrole {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (n && n->role == a11y::Role::Tab) return NSAccessibilityTabButtonSubrole;
  return nil;
}

- (NSString*)accessibilityRoleDescription {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (!n) return nil;
  if (n->role == a11y::Role::Heading) return @"heading";
  if (n->role == a11y::Role::Dialog) return @"dialog";
  if (n->role == a11y::Role::MenuItem) return @"menu item";
  if (n->role == a11y::Role::Group && n->tag.rfind("msg:", 0) == 0) return @"message";
  return NSAccessibilityRoleDescription([self accessibilityRole], [self accessibilitySubrole]);
}

- (NSString*)accessibilityLabel {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (!n || n->label.empty()) return nil;
  return [NSString stringWithUTF8String:n->label.c_str()];
}

- (NSString*)accessibilityTitle {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (!n || n->role != a11y::Role::Heading) return nil;
  return [NSString stringWithUTF8String:n->label.c_str()];
}

- (id)accessibilityValue {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (!n) return nil;
  if (n->checkable) return @(n->checked ? 1 : 0);
  if (n->role == a11y::Role::Tab) return @(n->selected ? 1 : 0);
  if (n->role == a11y::Role::Disclosure) return @(n->expanded ? 1 : 0);
  if (n->role == a11y::Role::PopUpButton && n->value.empty()) return nil;
  if (n->role == a11y::Role::StaticText || n->role == a11y::Role::TextField || n->role == a11y::Role::PopUpButton) {
    return [NSString stringWithUTF8String:(n->value.empty() && n->role == a11y::Role::StaticText ? n->label : n->value).c_str()] ?: @"";
  }
  return nil;
}

- (NSString*)accessibilityHelp {
  const a11y::Node* n = nodeFor(self.host, self.key);
  if (!n || n->help.empty()) return nil;
  return [NSString stringWithUTF8String:n->help.c_str()];
}

- (BOOL)isAccessibilityEnabled {
  const a11y::Node* n = nodeFor(self.host, self.key);
  return n && n->enabled;
}

- (BOOL)isAccessibilitySelected {
  const a11y::Node* n = nodeFor(self.host, self.key);
  return n && n->selected;
}

- (BOOL)isAccessibilityExpanded {
  const a11y::Node* n = nodeFor(self.host, self.key);
  return n && n->expanded;
}

- (BOOL)isAccessibilityFocused {
  const a11y::Node* n = nodeFor(self.host, self.key);
  return n && n->focused;
}

- (void)setAccessibilityFocused:(BOOL)focused {
  if (!focused) return;
  const uint64_t key = self.key;
  native::runInWindow((__bridge void*)self.host.window, [key] { a11y::ui::requestFocus(key); });
}

- (NSRect)accessibilityFrame {
  const a11y::Node* n = nodeFor(self.host, self.key);
  OWUIMetalView* host = self.host;
  if (!n || !host) return NSZeroRect;
  return [host screenRect:n->frame];
}

- (id)accessibilityParent {
  const a11y::Node* n = nodeFor(self.host, self.key);
  OWUIMetalView* host = self.host;
  if (!n || !host) return host;
  if (n->parent < 0) return host;
  return [host elementForKey:snapOf(host).nodes[static_cast<size_t>(n->parent)].key];
}

- (NSArray*)accessibilityChildren {
  const a11y::Node* n = nodeFor(self.host, self.key);
  OWUIMetalView* host = self.host;
  if (!n || !host || n->children.empty()) return @[];
  NSMutableArray* out = [NSMutableArray arrayWithCapacity:n->children.size()];
  const a11y::Snapshot& snap = snapOf(host);
  for (int child : n->children) [out addObject:[host elementForKey:snap.nodes[static_cast<size_t>(child)].key]];
  return out;
}

- (BOOL)accessibilityPerformPress {
  const a11y::Node* n = nodeFor(self.host, self.key);
  OWUIMetalView* host = self.host;
  if (!n || !n->enabled) return NO;
  if (n->role == a11y::Role::Group || n->role == a11y::Role::StaticText || n->role == a11y::Role::Heading ||
      n->role == a11y::Role::Dialog) {
    const uint64_t key = self.key;
    native::runInWindow((__bridge void*)host.window, [key] { a11y::ui::requestFocus(key); });
    return !n->tag.empty();
  }
  // A text field takes typing through ImGui's own key responder, so hand it
  // the keyboard first (the native composer may hold it).
  if (n->role == a11y::Role::TextField && host) {
    Class responder = NSClassFromString(@"KeyEventResponder");
    for (NSView* sub in host.subviews) {
      if (responder && [sub isKindOfClass:responder]) [host.window makeFirstResponder:sub];
    }
  }
  ImGuiContext* context = host.imguiContext ? static_cast<ImGuiContext*>(host.imguiContext) : ImGui::GetCurrentContext();
  a11y::ui::requestPress(context, self.key);
  return YES;
}

- (NSArray<NSAccessibilityCustomAction*>*)accessibilityCustomActions { return nil; }

@end

@implementation OWUIMetalView

- (NSMutableDictionary<NSNumber*, OWUIAXElement*>*)elements {
  if (!_elements) _elements = [NSMutableDictionary dictionary];
  return _elements;
}

- (OWUIAXElement*)elementForKey:(uint64_t)key {
  NSNumber* k = @(key);
  OWUIAXElement* element = self.elements[k];
  if (!element) {
    element = [[OWUIAXElement alloc] init];
    element.key = key;
    element.host = self;
    self.elements[k] = element;
  }
  return element;
}

- (NSRect)screenRect:(const a11y::Rect&)r {
  const NSRect local = NSMakeRect(r.x, self.bounds.size.height - r.y - r.h, r.w, r.h);
  const NSRect inWindow = [self convertRect:local toView:nil];
  return self.window ? [self.window convertRectToScreen:inWindow] : inWindow;
}

- (BOOL)isAccessibilityElement { return YES; }
- (BOOL)isAccessibilityEnabled { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityGroupRole; }
- (NSString*)accessibilityLabel { return @"Open WebUI"; }

// Native subviews (the composer, the artifact and PDF views) that are on
// screen and have something to say.
- (NSArray*)nativeChildren {
  NSMutableArray* out = [NSMutableArray array];
  for (NSView* sub in self.subviews) {
    if (sub.hidden || sub.alphaValue <= 0.01) continue;
    [out addObjectsFromArray:NSAccessibilityUnignoredChildren(@[ sub ])];
  }
  return out;
}

- (NSArray*)accessibilityChildren {
  gLastQuery = nowSeconds();
  const a11y::Snapshot& snap = snapOf(self);
  NSMutableArray* out = [NSMutableArray arrayWithCapacity:snap.roots.size() + 2];
  for (int root : snap.roots) [out addObject:[self elementForKey:snap.nodes[static_cast<size_t>(root)].key]];
  // A modal hides the rest of the window, the native views included.
  const bool modal = snap.roots.size() == 1 && snap.nodes[0].role == a11y::Role::Dialog;
  if (!modal) [out addObjectsFromArray:[self nativeChildren]];
  return out;
}

- (id)accessibilityHitTest:(NSPoint)point {
  gLastQuery = nowSeconds();
  if (!self.window) return self;
  const NSPoint inWindow = [self.window convertPointFromScreen:point];
  for (NSView* sub in self.subviews.reverseObjectEnumerator) {
    if (sub.hidden || NSAccessibilityUnignoredChildren(@[ sub ]).count == 0) continue;
    if (NSPointInRect([sub convertPoint:inWindow fromView:nil], sub.bounds)) {
      id hit = [sub accessibilityHitTest:point];
      if (hit) return NSAccessibilityUnignoredAncestor(hit);
    }
  }
  const NSPoint local = [self convertPoint:inWindow fromView:nil];
  const a11y::Snapshot& snap = snapOf(self);
  const int index = snap.hitTest(static_cast<float>(local.x), static_cast<float>(self.bounds.size.height - local.y));
  if (index < 0) return self;
  return [self elementForKey:snap.nodes[static_cast<size_t>(index)].key];
}

- (id)accessibilityFocusedUIElement {
  const a11y::Snapshot& snap = snapOf(self);
  const int index = snap.focusedIndex();
  if (index < 0) return self;
  return [self elementForKey:snap.nodes[static_cast<size_t>(index)].key];
}

@end

// --- C++ side -----------------------------------------------------------------

namespace a11y::bridge {
namespace {

double gLastDebugWrite = 0;

std::string toStd(id value) {
  if (!value) return "";
  if ([value isKindOfClass:[NSString class]]) return [(NSString*)value UTF8String] ?: "";
  if ([value isKindOfClass:[NSNumber class]]) return [[(NSNumber*)value stringValue] UTF8String];
  if ([value isKindOfClass:[NSAttributedString class]]) return [[(NSAttributedString*)value string] UTF8String] ?: "";
  return [[value description] UTF8String] ?: "";
}

// Proxies the system puts in the tree (title-bar cells) may not implement
// the method-based protocol; ask only what they answer.
id ask(id element, SEL selector) {
  if (![element respondsToSelector:selector]) return nil;
  return ((id (*)(id, SEL))objc_msgSend)(element, selector);
}

std::string line(id element) {
  std::string out = toStd(ask(element, @selector(accessibilityRole)));
  const std::string sub = toStd(ask(element, @selector(accessibilitySubrole)));
  if (!sub.empty()) out += "/" + sub;
  const std::string label = toStd(ask(element, @selector(accessibilityLabel)));
  const std::string title = toStd(ask(element, @selector(accessibilityTitle)));
  if (!label.empty()) out += " \"" + label + "\"";
  else if (!title.empty()) out += " title=\"" + title + "\"";
  std::string value = toStd(ask(element, @selector(accessibilityValue)));
  if (!value.empty()) {
    if (value.size() > 80) value = value.substr(0, 80) + "...";
    for (char& c : value) if (c == '\n') c = ' ';
    out += " = \"" + value + "\"";
  }
  if ([element respondsToSelector:@selector(isAccessibilityEnabled)] && ![element isAccessibilityEnabled]) out += " [disabled]";
  if ([element respondsToSelector:@selector(isAccessibilityFocused)] && [element isAccessibilityFocused]) out += " [focused]";
  const NSRect f = [element respondsToSelector:@selector(accessibilityFrame)] ? [element accessibilityFrame] : NSZeroRect;
  char buf[80];
  snprintf(buf, sizeof buf, " @%.0f,%.0f %.0fx%.0f", f.origin.x, f.origin.y, f.size.width, f.size.height);
  return out + buf;
}

template <typename Visit>
bool walk(id element, int depth, int* budget, Visit&& visit) {
  if (!element || *budget <= 0 || depth > 14) return false;
  --*budget;
  if (visit(element, depth)) return true;
  NSArray* children = ask(element, @selector(accessibilityChildren));
  for (id child in children ?: @[]) {
    if (walk(child, depth + 1, budget, visit)) return true;
  }
  return false;
}

id findByLabel(OWUIMetalView* view, const std::string& label) {
  id found = nil;
  int budget = 12000;
  // Every window of the app (#121), the key one first.
  NSMutableArray* windows = [NSMutableArray array];
  if (NSApp.keyWindow) [windows addObject:NSApp.keyWindow];
  for (NSWindow* w in NSApp.windows) {
    if (w.visible && ![windows containsObject:w]) [windows addObject:w];
  }
  if (windows.count == 0 && view.window) [windows addObject:view.window];
  for (NSWindow* w in windows) {
    if (found) break;
    walk(w, 0, &budget, [&](id element, int) {
    if (toStd(ask(element, @selector(accessibilityLabel))) == label || toStd(ask(element, @selector(accessibilityTitle))) == label) {
      found = element;
      return true;
    }
    return false;
    });
  }
  return found;
}

void writeFile(const std::string& path, const std::string& text) {
  const std::string tmp = path + ".tmp";
  { std::ofstream(tmp) << text; }
  std::rename(tmp.c_str(), path.c_str());
}

// OWUI_A11Y_DEBUG=<dir>: tree.txt each second, announcements.txt appended,
// commands read from cmd.txt ("press <label>", "focus <label>", "focused")
// with results appended to cmd.log. A test hook for checking the tree from
// outside without giving another process Accessibility access.
void debugChannel(OWUIMetalView* view, const std::vector<std::string>& announced) {
  static const char* dir = std::getenv("OWUI_A11Y_DEBUG");
  if (!dir || !*dir) return;
  const std::string base(dir);
  if (!announced.empty()) {
    std::ofstream out(base + "/announcements.txt", std::ios::app);
    for (const std::string& a : announced) out << a << "\n";
  }
  const double now = nowSeconds();
  if (now - gLastDebugWrite < 1.0) return;
  gLastDebugWrite = now;
  std::string all;
  for (NSWindow* w in NSApp.windows) {
    if (!w.visible || ![w.contentView isKindOfClass:[OWUIMetalView class]]) continue;
    all += dumpWindow((__bridge void*)w.contentView);
  }
  writeFile(base + "/tree.txt", all);
  std::ifstream cmdFile(base + "/cmd.txt");
  if (!cmdFile) return;
  std::string command, log;
  while (std::getline(cmdFile, command)) {
    if (command.empty()) continue;
    bool ok = false;
    if (command.rfind("press ", 0) == 0) ok = pressLabel((__bridge void*)view, command.substr(6));
    else if (command.rfind("focus ", 0) == 0) ok = focusLabel((__bridge void*)view, command.substr(6));
    else if (command == "focused") { log += "focused: " + describeFocused((__bridge void*)view) + "\n"; ok = true; }
    log += command + (ok ? " -> ok\n" : " -> not found\n");
  }
  cmdFile.close();
  std::remove((base + "/cmd.txt").c_str());
  std::ofstream(base + "/cmd.log", std::ios::app) << log;
}

}  // namespace

void frameDone(void* metalView) {
  OWUIMetalView* view = (__bridge OWUIMetalView*)metalView;
  if (!view) return;
  const a11y::Snapshot& snap = snapOf(view);
  const double now = nowSeconds();
  static const bool debugOn = std::getenv("OWUI_A11Y_DEBUG") && *std::getenv("OWUI_A11Y_DEBUG");
  const bool listening = now - gLastQuery < 120.0 || NSWorkspace.sharedWorkspace.voiceOverEnabled || debugOn;
  a11y::ui::setListening(listening);

  // Layout: nodes came or went. Throttled, a streaming reply would otherwise
  // post every frame.
  const uint64_t hash = snap.structureHash();
  if (hash != view.lastHash && now - view.lastLayoutPost > 0.5) {
    view.lastHash = hash;
    view.lastLayoutPost = now;
    if (listening) NSAccessibilityPostNotification(view, NSAccessibilityLayoutChangedNotification);
  }
  // Focus: ImGui's nav cursor or the focused message moved.
  const int focused = snap.focusedIndex();
  const uint64_t focusKey = focused >= 0 ? snap.nodes[static_cast<size_t>(focused)].key : 0;
  if (focusKey != view.lastFocusKey) {
    view.lastFocusKey = focusKey;
    if (focusKey && listening) NSAccessibilityPostNotification([view elementForKey:focusKey], NSAccessibilityFocusedUIElementChangedNotification);
  }
  const std::vector<std::string> announced = a11y::ui::takeAnnouncements();
  for (const std::string& text : announced) {
    NSString* s = [NSString stringWithUTF8String:text.c_str()];
    if (!s) continue;
    NSAccessibilityPostNotificationWithUserInfo(NSApp, NSAccessibilityAnnouncementRequestedNotification,
                                                @{NSAccessibilityAnnouncementKey : s,
                                                  NSAccessibilityPriorityKey : @(NSAccessibilityPriorityHigh)});
  }
  // Drop elements whose nodes are gone (every couple of seconds).
  if (a11y::ui::frameNumber() % 120 == 0 && view.elements.count > snap.nodes.size() + 64) {
    NSMutableArray* stale = [NSMutableArray array];
    for (NSNumber* k in view.elements) {
      if (!snap.find(k.unsignedLongLongValue)) [stale addObject:k];
    }
    [view.elements removeObjectsForKeys:stale];
  }
  debugChannel(view, announced);
}

std::string dumpWindow(void* metalView, int maxNodes) {
  OWUIMetalView* view = (__bridge OWUIMetalView*)metalView;
  if (!view) return "";
  std::string out;
  int budget = maxNodes;
  id root = view.window ?: (id)view;
  walk(root, 0, &budget, [&](id element, int depth) {
    out.append(static_cast<size_t>(depth) * 2, ' ');
    out += line(element) + "\n";
    return false;
  });
  if (budget <= 0) out += "(truncated)\n";
  return out;
}

bool pressLabel(void* metalView, const std::string& label) {
  id element = findByLabel((__bridge OWUIMetalView*)metalView, label);
  return element && [element respondsToSelector:@selector(accessibilityPerformPress)] && [element accessibilityPerformPress];
}

bool focusLabel(void* metalView, const std::string& label) {
  id element = findByLabel((__bridge OWUIMetalView*)metalView, label);
  if (!element || ![element respondsToSelector:@selector(setAccessibilityFocused:)]) return false;
  [element setAccessibilityFocused:YES];
  return true;
}

std::string describeFocused(void* metalView) {
  OWUIMetalView* view = (__bridge OWUIMetalView*)metalView;
  if (!view.window) return "";
  id focused = [view.window accessibilityFocusedUIElement];
  // The window hands back its first responder's unignored ancestor; ours
  // answers with the focused node.
  if (focused == view) focused = [view accessibilityFocusedUIElement];
  return focused ? line(focused) : "(none)";
}

}  // namespace a11y::bridge
