// Shortcut editor (#126): every binding as a chord string ("cmd+shift+k"),
// a table of defaults, overrides as JSON, conflict detection, the native
// menu key equivalent for a chord, and the Vim-keys state machine for the
// transcript. App-free; --keymaptest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace keymap {

using Json = nlohmann::json;

struct Chord {
  bool cmd = false, shift = false, alt = false, ctrl = false;
  std::string key;   // "a".."z", "0".."9", "-", "=", ".", ",", "/", ";", "'", "[", "]", "`", "f1".."f12", "enter", "space", "tab", "escape", "up", "down", "left", "right", "backspace", "delete"
  bool empty() const { return key.empty(); }
};
bool parse(const std::string& text, Chord* out);   // "Cmd+Shift+K", "⌘⇧K" style names accepted; false when unusable
std::string format(const Chord& chord);            // canonical "cmd+shift+k"
std::string pretty(const Chord& chord);            // "⌘⇧K"

struct Shortcut {
  std::string id, label, group;
  std::string def;     // default chord
  std::string chord;   // current ("" = unbound)
  bool menu = false;   // owned by a native menu item (its key equivalent follows the chord)
};
std::vector<Shortcut> defaults();
const Shortcut* find(const std::vector<Shortcut>& list, const std::string& id);
void applyOverrides(std::vector<Shortcut>& list, const Json& overrides);   // {id: chord}; unknown ids and bad chords ignored
Json overrides(const std::vector<Shortcut>& list);                         // only bindings that differ from their default

struct Conflict {
  std::string a, b, chord;   // shortcut ids sharing the chord
};
std::vector<Conflict> conflicts(const std::vector<Shortcut>& list);

// For NSMenuItem: the key equivalent character and modifier bits (1 cmd, 2 shift, 4 alt, 8 ctrl).
bool menuEquivalent(const Chord& chord, std::string* key, int* mods);

enum class VimAction { None, Down, Up, Top, Bottom, Search, Copy, Compose, Regenerate, Pin, Speak, Edit, Escape };
struct VimState {
  bool pendingG = false;
  double pendingAt = 0.0;   // seconds
};
// `key` is a key name as above ("g", "j", "slash" is "/"); a second "g" within a second is "gg".
VimAction vimStep(VimState& state, const std::string& key, bool shift, double now);
const char* vimActionName(VimAction action);

std::string cheatSheet(const std::vector<Shortcut>& list, bool vimKeys);   // plain text, grouped

}  // namespace keymap
