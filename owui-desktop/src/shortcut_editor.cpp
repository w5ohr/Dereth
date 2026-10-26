// Shortcut editor in the app (#126): the keymap loaded from settings, native
// menu key equivalents kept in step, chord detection for the ImGui-handled
// shortcuts, the Vim keys for the transcript, the Settings card with capture
// and conflict marks, and the cheat sheet (Cmd+/).
#include <algorithm>

#include "app.h"
#include "core/common.h"
#include "core/keymap.h"
#include "core/settings.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<keymap::Shortcut> gMap;
bool gLoaded = false, gMenusApplied = false;
std::string gCapturing;   // shortcut id waiting for keys
keymap::VimState gVim;

struct NamedKey {
  const char* name;
  ImGuiKey key;
};
const std::vector<NamedKey>& namedKeys() {
  static const std::vector<NamedKey> keys = [] {
    std::vector<NamedKey> v;
    static std::string letters[26], digits[10], fkeys[12];
    for (int i = 0; i < 26; ++i) {
      letters[i] = std::string(1, static_cast<char>('a' + i));
      v.push_back({letters[i].c_str(), static_cast<ImGuiKey>(ImGuiKey_A + i)});
    }
    for (int i = 0; i < 10; ++i) {
      digits[i] = std::string(1, static_cast<char>('0' + i));
      v.push_back({digits[i].c_str(), static_cast<ImGuiKey>(ImGuiKey_0 + i)});
    }
    for (int i = 0; i < 12; ++i) {
      fkeys[i] = "f" + std::to_string(i + 1);
      v.push_back({fkeys[i].c_str(), static_cast<ImGuiKey>(ImGuiKey_F1 + i)});
    }
    v.push_back({"-", ImGuiKey_Minus});
    v.push_back({"=", ImGuiKey_Equal});
    v.push_back({".", ImGuiKey_Period});
    v.push_back({",", ImGuiKey_Comma});
    v.push_back({"/", ImGuiKey_Slash});
    v.push_back({";", ImGuiKey_Semicolon});
    v.push_back({"'", ImGuiKey_Apostrophe});
    v.push_back({"[", ImGuiKey_LeftBracket});
    v.push_back({"]", ImGuiKey_RightBracket});
    v.push_back({"`", ImGuiKey_GraveAccent});
    v.push_back({"enter", ImGuiKey_Enter});
    v.push_back({"space", ImGuiKey_Space});
    v.push_back({"tab", ImGuiKey_Tab});
    v.push_back({"escape", ImGuiKey_Escape});
    v.push_back({"up", ImGuiKey_UpArrow});
    v.push_back({"down", ImGuiKey_DownArrow});
    v.push_back({"left", ImGuiKey_LeftArrow});
    v.push_back({"right", ImGuiKey_RightArrow});
    v.push_back({"backspace", ImGuiKey_Backspace});
    v.push_back({"delete", ImGuiKey_Delete});
    return v;
  }();
  return keys;
}

// With ConfigMacOSXBehaviors ImGui swaps the two: io.KeyCtrl is the Command key.
bool cmdDown(const ImGuiIO& io) { return io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper; }
bool ctrlDown(const ImGuiIO& io) { return io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl; }

ImGuiKey keyFor(const std::string& name) {
  for (const NamedKey& k : namedKeys()) {
    if (name == k.name) return k.key;
  }
  return ImGuiKey_None;
}

void load() {
  if (gLoaded) return;
  gLoaded = true;
  gMap = keymap::defaults();
  keymap::applyOverrides(gMap, core::Settings::instance().extra.value("keymap", keymap::Json::object()));
}

void save() {
  core::Settings::instance().extra["keymap"] = keymap::overrides(gMap);
  core::Settings::instance().save();
  gMenusApplied = false;
}

}  // namespace

void App::shortcutsApplyMenus() {
  load();
  for (const keymap::Shortcut& s : gMap) {
    if (!s.menu) continue;
    keymap::Chord c;
    std::string key;
    int mods = 0;
    if (keymap::parse(s.chord, &c) && keymap::menuEquivalent(c, &key, &mods)) native::setMenuShortcut(s.id, key, mods);
    else native::setMenuShortcut(s.id, "", 0);
  }
  gMenusApplied = true;
}

bool App::shortcutCapturing() const { return !gCapturing.empty(); }

bool App::shortcutPressed(const std::string& id) const {
  load();
  const keymap::Shortcut* s = keymap::find(gMap, id);
  keymap::Chord c;
  if (!s || !keymap::parse(s->chord, &c)) return false;
  const ImGuiIO& io = ImGui::GetIO();
  if (cmdDown(io) != c.cmd || io.KeyShift != c.shift || io.KeyAlt != c.alt || ctrlDown(io) != c.ctrl) return false;
  const ImGuiKey key = keyFor(c.key);
  return key != ImGuiKey_None && ImGui::IsKeyPressed(key, false);
}

std::string App::shortcutLabel(const std::string& id) const {
  load();
  const keymap::Shortcut* s = keymap::find(gMap, id);
  keymap::Chord c;
  if (!s || !keymap::parse(s->chord, &c)) return "";
  return keymap::pretty(c);
}

bool App::handleVimKeys(const std::vector<std::string>& path, int index) {
  if (!settings().extra.value("vimKeys", false) || path.empty()) return false;
  const ImGuiIO& io = ImGui::GetIO();
  std::string pressed;
  for (const char* name : {"j", "k", "g", "/", "y", "i", "a", "o", "r", "p", "s", "e", "escape"}) {
    if (ImGui::IsKeyPressed(keyFor(name), name[0] == 'j' || name[0] == 'k')) {
      pressed = name;
      break;
    }
  }
  if (pressed.empty()) return false;
  const keymap::VimAction action = keymap::vimStep(gVim, pressed, io.KeyShift, core::nowMillis() / 1000.0);
  const int last = static_cast<int>(path.size()) - 1;
  auto focus = [&](int i) {
    focusedMessageId = path[std::clamp(i, 0, last)];
    pendingScrollTo = focusedMessageId;
  };
  const Message& m = chat.messages.at(focusedMessageId);
  switch (action) {
    case keymap::VimAction::Down: focus(index + 1); break;
    case keymap::VimAction::Up: focus(index - 1); break;
    case keymap::VimAction::Top: focus(0); break;
    case keymap::VimAction::Bottom: focus(last); break;
    case keymap::VimAction::Search: showChatSearch = true; break;
    case keymap::VimAction::Copy:
      platform::setClipboardText(m.content);
      widgets::toast(widgets::ToastLevel::Success, "Copied");
      break;
    case keymap::VimAction::Compose:
      focusedMessageId.clear();
      focusComposer = true;
      break;
    case keymap::VimAction::Regenerate:
      if (m.role == "assistant") regenerate(focusedMessageId);
      break;
    case keymap::VimAction::Pin: togglePin(focusedMessageId); break;
    case keymap::VimAction::Speak:
      if (m.role == "assistant") readAloud(focusedMessageId, m.content);
      break;
    case keymap::VimAction::Edit:
      if (m.role == "user") {
        chat.messages[focusedMessageId].editing = true;
        chat.messages[focusedMessageId].editBuffer = m.content;
      }
      break;
    case keymap::VimAction::Escape: focusedMessageId.clear(); break;
    default: break;
  }
  return true;   // the key was a Vim key (or the first g); the legacy letters stay out of the way
}

void App::drawShortcutsSection(float width) {
  load();
  if (!gMenusApplied && gCapturing.empty()) shortcutsApplyMenus();
  const auto& palette = theme::palette();
  card("shortcuts", [&] {
    ImGui::PushTextWrapPos(width - 20.0f);
    ImGui::TextUnformatted("Click a shortcut and press the new keys (Escape cancels, Backspace unbinds). Menu items follow their shortcut; two actions on the same keys are both flagged.");
    ImGui::PopTextWrapPos();
    // Capture.
    if (!gCapturing.empty()) {
      const ImGuiIO& io = ImGui::GetIO();
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        gCapturing.clear();
        shortcutsApplyMenus();
      } else if ((ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) && !io.KeySuper && !io.KeyCtrl && !io.KeyAlt) {
        for (keymap::Shortcut& s : gMap) {
          if (s.id == gCapturing) s.chord.clear();
        }
        save();
        gCapturing.clear();
      } else {
        for (const NamedKey& k : namedKeys()) {
          if (k.key == ImGuiKey_Escape || k.key == ImGuiKey_Backspace || k.key == ImGuiKey_Delete) continue;
          if (!ImGui::IsKeyPressed(k.key, false)) continue;
          const bool fkey = k.name[0] == 'f' && k.name[1] != '\0';
          if (!fkey && !io.KeySuper && !io.KeyCtrl && !io.KeyAlt) {
            widgets::toastInfo("Use Cmd, Ctrl or Option with the key (a bare key would steal typing)");
            break;
          }
          keymap::Chord c;
          c.cmd = cmdDown(io);
          c.shift = io.KeyShift;
          c.alt = io.KeyAlt;
          c.ctrl = ctrlDown(io);
          c.key = k.name;
          for (keymap::Shortcut& s : gMap) {
            if (s.id == gCapturing) s.chord = keymap::format(c);
          }
          save();
          gCapturing.clear();
          break;
        }
      }
    }
    const std::vector<keymap::Conflict> conflicts = keymap::conflicts(gMap);
    auto conflictWith = [&](const std::string& id) -> std::string {
      for (const keymap::Conflict& c : conflicts) {
        if (c.a == id) return keymap::find(gMap, c.b) ? keymap::find(gMap, c.b)->label : c.b;
        if (c.b == id) return keymap::find(gMap, c.a) ? keymap::find(gMap, c.a)->label : c.a;
      }
      return "";
    };
    if (ImGui::BeginTable("##keymap", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 20.0f, 0))) {
      ImGui::TableSetupColumn("Action");
      ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
      ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableHeadersRow();
      std::string group;
      for (keymap::Shortcut& s : gMap) {
        ImGui::PushID(s.id.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (s.group != group) {
          group = s.group;
          widgets::dimText("%s", group.c_str());
          ImGui::SameLine();
        }
        ImGui::TextUnformatted(s.label.c_str());
        ImGui::TableSetColumnIndex(1);
        keymap::Chord c;
        const bool bound = keymap::parse(s.chord, &c);
        const std::string label = gCapturing == s.id ? "Press keys..." : bound ? keymap::pretty(c) : "(unbound)";
        const std::string other = conflictWith(s.id);
        if (!other.empty()) ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
        if (ImGui::Button(label.c_str(), ImVec2(140, 0))) {
          gCapturing = gCapturing == s.id ? "" : s.id;
          // The menu would swallow its own key equivalents before the editor saw them.
          if (gCapturing.empty()) shortcutsApplyMenus();
          else native::suspendMenuShortcuts();
        }
        if (!other.empty()) ImGui::PopStyleColor();
        if (!other.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("Also bound to: %s", other.c_str());
        ImGui::TableSetColumnIndex(2);
        keymap::Chord d;
        widgets::dimText("%s", keymap::parse(s.def, &d) ? keymap::pretty(d).c_str() : "-");
        ImGui::TableSetColumnIndex(3);
        if (s.chord != s.def && ImGui::SmallButton("Reset")) {
          s.chord = s.def;
          save();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (!conflicts.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::Text("%zu conflict%s - hover a red shortcut to see the other action.", conflicts.size(), conflicts.size() == 1 ? "" : "s");
      ImGui::PopStyleColor();
    }
    if (ImGui::SmallButton("Reset all")) {
      for (keymap::Shortcut& s : gMap) s.chord = s.def;
      save();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Cheat sheet")) showCheatSheet = true;
    bool vim = settings().extra.value("vimKeys", false);
    if (ImGui::Checkbox("Vim keys in the transcript: j / k move, gg / G jump, / finds, y copies, i returns to the composer (r, p, s, e as before)", &vim)) {
      settings().extra["vimKeys"] = vim;
      settings().save();
    }
    widgets::dimText("Shortcuts on the Edit menu (undo, copy, paste...) and Hide / Quit are macOS's own and stay fixed.");
  }, width);
}

void App::drawCheatSheet() {
  if (!showCheatSheet) return;
  load();
  ImGui::OpenPopup("Keyboard shortcuts");
  ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Keyboard shortcuts", &showCheatSheet)) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    showCheatSheet = false;
    ImGui::CloseCurrentPopup();
  }
  const std::string sheet = keymap::cheatSheet(gMap, settings().extra.value("vimKeys", false));
  ImGui::BeginChild("##sheet", ImVec2(0, -34.0f));
  theme::pushMono();
  ImGui::TextUnformatted(sheet.c_str());
  theme::pop();
  ImGui::EndChild();
  if (ImGui::Button("Copy")) platform::setClipboardText(sheet);
  ImGui::SameLine();
  if (ImGui::Button("Edit shortcuts")) {
    showCheatSheet = false;
    ImGui::CloseCurrentPopup();
    pane = Pane::Settings;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    showCheatSheet = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace app
