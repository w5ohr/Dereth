// The ImGui side of the accessibility tree (#142): the item hooks that record
// every labelled widget as it is drawn, the calls panes use to add headings,
// text and message groups, and the click injection behind VoiceOver's press.
// Everything here runs on the main thread except announce(), which toasts
// may call from a worker.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/a11y.h"
#include "imgui.h"

namespace a11y::ui {

void install(ImGuiContext* context);
void forget(ImGuiContext* context);    // a window closed: drop its tree
// Each ImGui context (each window, #121) has its own tree; snapshot() and
// the rest act on the current context's.
const Snapshot& snapshotFor(ImGuiContext* context);
void requestPress(ImGuiContext* context, uint64_t key);
// An assistive app is reading the window (or the debug channel is on): the
// panes add the costlier nodes, such as every message's plain text.
void setListening(bool listening);
bool listening();   // turns the item hooks on for that context
void beginFrame();                     // after ImGui::NewFrame
void endFrame();                       // after the app has drawn, before ImGui::Render
const Snapshot& snapshot();            // the last finished frame
uint64_t frameNumber();

// The last item drawn: give it a name (icon-only buttons), mark it selected
// (rail items), or add a help string. Ignored when it was not visible.
void nameLastItem(const std::string& label, bool replace = false);
void markLastItem(bool selected);
void helpLastItem(const std::string& help);

// A group around what comes next; end it with the drawn rectangle (or none:
// the union of what it holds). Group closes itself at scope exit, so an
// early `continue` cannot leave it open.
int beginGroup(std::string_view key, Role role, const std::string& label, const std::string& value = "",
               const std::string& tag = "");
void endGroup(ImVec2 min = ImVec2(0, 0), ImVec2 max = ImVec2(0, 0));
class Group {
 public:
  Group(std::string_view key, Role role, const std::string& label, const std::string& value = "",
        const std::string& tag = "");
  ~Group();
  void close(ImVec2 min, ImVec2 max);
  void setFocused(bool focused);

 private:
  int depth_ = 0;
  int index_ = -1;
  bool open_ = false;
};

// Static text and headings for the last ImGui text item (or an explicit rectangle).
void textItem(const std::string& text);
void headingItem(const std::string& text);
void linkItem(const std::string& text);   // a clickable text drawn without an ImGui ID
// evenIfHidden: keep it while scrolled away (message text, so VoiceOver can
// read a whole conversation without scrolling first).
void staticText(const std::string& label, const std::string& value, ImVec2 min, ImVec2 max, bool evenIfHidden = false);

// Spoken by VoiceOver when it is running (queued; any thread).
void announce(const std::string& text);
std::vector<std::string> takeAnnouncements();

// VoiceOver asked to press a node: a click is played into ImGui at its centre
// over the next three frames. pressInFlight() tells the frame loop to leave
// the pointer alone meanwhile.
void requestPress(uint64_t key);
bool pressInFlight();
void pumpInput(ImGuiIO& io);   // before ImGui::NewFrame
uint64_t lastPressedKey();

// Keyboard focus onto the first control inside the group drawn with `key`
// last frame (View > Next Region). False when it holds none.
bool focusFirstIn(std::string_view key);
uint64_t groupKey(std::string_view key);   // the node key beginGroup gives `key` in the root window

// VoiceOver moved its cursor onto a node that carries a tag (a message): the
// app scrolls it into view and makes it the focused message.
void setFocusHandler(std::function<void(const std::string& tag)> handler);
void requestFocus(uint64_t key);

}  // namespace a11y::ui
