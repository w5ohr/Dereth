#include "keymap.h"

#include <algorithm>
#include <map>

#include "common.h"

namespace keymap {
namespace {

const std::vector<std::string>& keyNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> v;
    for (char c = 'a'; c <= 'z'; ++c) v.push_back(std::string(1, c));
    for (char c = '0'; c <= '9'; ++c) v.push_back(std::string(1, c));
    for (const char* s : {"-", "=", ".", ",", "/", ";", "'", "[", "]", "`", "enter", "space", "tab", "escape", "up", "down", "left", "right", "backspace", "delete"}) v.push_back(s);
    for (int i = 1; i <= 12; ++i) v.push_back("f" + std::to_string(i));
    return v;
  }();
  return names;
}

std::string canonicalKey(std::string k) {
  k = core::lower(core::trim(k));
  static const std::map<std::string, std::string> alias{{"return", "enter"}, {"esc", "escape"}, {"minus", "-"}, {"period", "."}, {"comma", ","}, {"slash", "/"}, {"plus", "="},
                                                        {"equal", "="}, {"equals", "="}, {"semicolon", ";"}, {"quote", "'"}, {"apostrophe", "'"}, {"grave", "`"}, {"backtick", "`"},
                                                        {"uparrow", "up"}, {"downarrow", "down"}, {"leftarrow", "left"}, {"rightarrow", "right"}, {"del", "delete"}, {"bs", "backspace"},
                                                        {"⏎", "enter"}, {"↑", "up"}, {"↓", "down"}, {"←", "left"}, {"→", "right"}, {"⎋", "escape"}, {"⌫", "backspace"}, {"␣", "space"}};
  auto it = alias.find(k);
  if (it != alias.end()) return it->second;
  return k;
}

}  // namespace

bool parse(const std::string& text, Chord* out) {
  Chord c;
  std::string t = core::trim(text);
  if (t.empty()) return false;
  // Symbol form: ⌘⇧⌥⌃ followed by the key.
  std::string rest;
  size_t i = 0;
  while (i < t.size()) {
    if (t.compare(i, 3, "\xe2\x8c\x98") == 0) { c.cmd = true; i += 3; }        // ⌘
    else if (t.compare(i, 3, "\xe2\x87\xa7") == 0) { c.shift = true; i += 3; } // ⇧
    else if (t.compare(i, 3, "\xe2\x8c\xa5") == 0) { c.alt = true; i += 3; }   // ⌥
    else if (t.compare(i, 3, "\xe2\x8c\x83") == 0) { c.ctrl = true; i += 3; }  // ⌃
    else break;
  }
  rest = t.substr(i);
  std::vector<std::string> parts;
  for (const std::string& p : core::split(rest, '+')) {
    if (!core::trim(p).empty()) parts.push_back(core::trim(p));
  }
  if (rest == "+" || core::endsWith(rest, "++")) parts.push_back("=");   // a literal plus key
  if (parts.empty()) return false;
  for (size_t p = 0; p + 1 < parts.size(); ++p) {
    const std::string m = core::lower(parts[p]);
    if (m == "cmd" || m == "command" || m == "meta" || m == "super") c.cmd = true;
    else if (m == "shift") c.shift = true;
    else if (m == "alt" || m == "option" || m == "opt") c.alt = true;
    else if (m == "ctrl" || m == "control") c.ctrl = true;
    else return false;
  }
  c.key = canonicalKey(parts.back());
  const auto& names = keyNames();
  if (std::find(names.begin(), names.end(), c.key) == names.end()) return false;
  *out = c;
  return true;
}

std::string format(const Chord& c) {
  if (c.empty()) return "";
  std::string out;
  if (c.ctrl) out += "ctrl+";
  if (c.alt) out += "alt+";
  if (c.shift) out += "shift+";
  if (c.cmd) out += "cmd+";
  return out + c.key;
}

std::string pretty(const Chord& c) {
  if (c.empty()) return "-";
  std::string out;
  if (c.ctrl) out += "\xe2\x8c\x83";
  if (c.alt) out += "\xe2\x8c\xa5";
  if (c.shift) out += "\xe2\x87\xa7";
  if (c.cmd) out += "\xe2\x8c\x98";
  static const std::map<std::string, std::string> names{{"enter", "\xe2\x8f\x8e"}, {"up", "\xe2\x86\x91"}, {"down", "\xe2\x86\x93"}, {"left", "\xe2\x86\x90"}, {"right", "\xe2\x86\x92"},
                                                        {"escape", "\xe2\x8e\x8b"}, {"backspace", "\xe2\x8c\xab"}, {"delete", "\xe2\x8c\xa6"}, {"space", "Space"}, {"tab", "Tab"}};
  auto it = names.find(c.key);
  if (it != names.end()) return out + it->second;
  std::string k = c.key;
  for (char& ch : k) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
  return out + k;
}

std::vector<Shortcut> defaults() {
  std::vector<Shortcut> v;
  auto add = [&](const char* id, const char* label, const char* group, const char* def, bool menu) {
    Shortcut s;
    s.id = id;
    s.label = label;
    s.group = group;
    s.def = def;
    s.chord = def;
    s.menu = menu;
    v.push_back(s);
  };
  add("palette", "Command palette", "General", "cmd+k", true);
  add("quickAsk", "Quick Ask", "General", "cmd+j", true);
  add("cheatSheet", "Shortcut cheat sheet", "General", "cmd+/", false);
  add("chat.new", "New chat", "Chat", "cmd+n", true);
  add("chat.newWindow", "Open chat in a new window", "Chat", "cmd+shift+n", true);
  add("chat.find", "Find in chat", "Chat", "cmd+f", true);
  add("chat.compare", "Compare answers", "Chat", "cmd+d", true);
  add("chat.pins", "Pinned messages", "Chat", "cmd+i", true);
  add("chat.inspector", "What was sent", "Chat", "cmd+e", true);
  add("chat.voice", "Voice conversation", "Chat", "cmd+u", true);
  add("chat.stop", "Stop generating", "Chat", "cmd+.", true);
  add("chat.refresh", "Refresh the chat list", "Chat", "cmd+r", false);
  add("chat.export", "Export chat as PDF", "Chat", "cmd+p", true);
  add("chat.capture", "Capture screen to chat", "Chat", "cmd+shift+2", true);
  add("view.sidebar", "Toggle the sidebar", "View", "cmd+b", true);
  add("view.float", "Float on top", "View", "cmd+shift+t", true);
  add("view.reading", "Reading mode", "View", "cmd+shift+r", false);
  add("view.focusComposer", "Focus the message field", "View", "cmd+l", false);
  add("view.nextRegion", "Next region (rail, chat list, conversation, message field)", "View", "cmd+alt+]", false);
  add("view.prevRegion", "Previous region", "View", "cmd+alt+[", false);
  add("view.layout1", "Layout 1 (Zen unless saved over)", "View", "cmd+alt+1", false);
  add("view.layout2", "Layout 2", "View", "cmd+alt+2", false);
  add("view.layout3", "Layout 3", "View", "cmd+alt+3", false);
  add("view.layout4", "Layout 4", "View", "cmd+alt+4", false);
  add("pane.chat", "Chats pane", "Panes", "cmd+1", true);
  add("pane.notes", "Notes pane", "Panes", "cmd+2", true);
  add("pane.channels", "Channels pane", "Panes", "cmd+3", true);
  add("pane.workspace", "Workspace pane", "Panes", "cmd+4", true);
  add("pane.images", "Images pane", "Panes", "cmd+5", true);
  add("pane.files", "Files pane", "Panes", "cmd+6", true);
  add("pane.memories", "Memories pane", "Panes", "cmd+7", true);
  add("pane.admin", "Admin pane", "Panes", "cmd+8", true);
  add("pane.console", "API console", "Panes", "cmd+9", true);
  add("pane.settings", "Settings pane", "Panes", "cmd+0", true);
  add("pane.tools", "Tools pane", "Panes", "cmd+-", true);
  add("pane.library", "Library pane", "Panes", "cmd+shift+l", false);
  return v;
}

const Shortcut* find(const std::vector<Shortcut>& list, const std::string& id) {
  for (const Shortcut& s : list) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

void applyOverrides(std::vector<Shortcut>& list, const Json& overrides) {
  if (!overrides.is_object()) return;
  for (Shortcut& s : list) {
    if (!overrides.contains(s.id) || !overrides[s.id].is_string()) continue;
    const std::string text = overrides[s.id].get<std::string>();
    if (text.empty()) {
      s.chord.clear();
      continue;
    }
    Chord c;
    if (parse(text, &c)) s.chord = format(c);
  }
}

Json overrides(const std::vector<Shortcut>& list) {
  Json out = Json::object();
  for (const Shortcut& s : list) {
    if (s.chord != s.def) out[s.id] = s.chord;
  }
  return out;
}

std::vector<Conflict> conflicts(const std::vector<Shortcut>& list) {
  std::vector<Conflict> out;
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].chord.empty()) continue;
    for (size_t j = i + 1; j < list.size(); ++j) {
      if (list[i].chord == list[j].chord) out.push_back({list[i].id, list[j].id, list[i].chord});
    }
  }
  return out;
}

bool menuEquivalent(const Chord& c, std::string* key, int* mods) {
  if (c.empty()) return false;
  static const std::map<std::string, std::string> special{{"enter", "\r"}, {"space", " "}, {"tab", "\t"}, {"escape", "\x1b"}, {"backspace", "\x08"}, {"delete", "\x7f"},
                                                          {"up", "\xef\x9c\x80"}, {"down", "\xef\x9c\x81"}, {"left", "\xef\x9c\x82"}, {"right", "\xef\x9c\x83"}};
  auto it = special.find(c.key);
  if (it != special.end()) *key = it->second;
  else if (c.key.size() >= 2 && c.key[0] == 'f' && isdigit(static_cast<unsigned char>(c.key[1]))) {
    // F1 = U+F704 ... in the Unicode private range NSF1FunctionKey.
    const int n = atoi(c.key.c_str() + 1);
    const unsigned cp = 0xF704 + static_cast<unsigned>(n - 1);
    std::string s;
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
    *key = s;
  } else {
    *key = c.key;
  }
  *mods = (c.cmd ? 1 : 0) | (c.shift ? 2 : 0) | (c.alt ? 4 : 0) | (c.ctrl ? 8 : 0);
  return true;
}

VimAction vimStep(VimState& state, const std::string& key, bool shift, double now) {
  const bool pending = state.pendingG && now - state.pendingAt < 1.0;
  state.pendingG = false;
  if (key == "g") {
    if (shift) return VimAction::Bottom;
    if (pending) return VimAction::Top;
    state.pendingG = true;
    state.pendingAt = now;
    return VimAction::None;
  }
  if (key == "j") return VimAction::Down;
  if (key == "k") return VimAction::Up;
  if (key == "/") return VimAction::Search;
  if (key == "y") return VimAction::Copy;
  if (key == "i" || key == "a" || key == "o") return VimAction::Compose;
  if (key == "r") return VimAction::Regenerate;
  if (key == "p") return VimAction::Pin;
  if (key == "s") return VimAction::Speak;
  if (key == "e") return VimAction::Edit;
  if (key == "escape") return VimAction::Escape;
  return VimAction::None;
}

const char* vimActionName(VimAction a) {
  switch (a) {
    case VimAction::Down: return "down";
    case VimAction::Up: return "up";
    case VimAction::Top: return "top";
    case VimAction::Bottom: return "bottom";
    case VimAction::Search: return "search";
    case VimAction::Copy: return "copy";
    case VimAction::Compose: return "compose";
    case VimAction::Regenerate: return "regenerate";
    case VimAction::Pin: return "pin";
    case VimAction::Speak: return "speak";
    case VimAction::Edit: return "edit";
    case VimAction::Escape: return "escape";
    default: return "none";
  }
}

std::string cheatSheet(const std::vector<Shortcut>& list, bool vimKeys) {
  std::string out;
  std::string group;
  for (const Shortcut& s : list) {
    if (s.group != group) {
      group = s.group;
      out += (out.empty() ? "" : "\n") + group + "\n";
    }
    Chord c;
    const std::string shown = parse(s.chord, &c) ? pretty(c) : "-";
    int columns = 0;   // code points, not bytes: the symbols are multi-byte
    for (unsigned char ch : shown) columns += (ch & 0xC0) != 0x80;
    out += "  " + shown + std::string(std::max<int>(1, 10 - columns), ' ') + s.label + "\n";
  }
  out += "\nTranscript (a message focused, no text field active)\n  " + std::string(vimKeys ? "j / k" : "\xe2\x86\x91 / \xe2\x86\x93") + "      move between messages\n";
  if (vimKeys) out += "  gg / G     first / last message\n  /          find in chat\n  y          copy\n  i          back to the composer\n";
  else out += "  c          copy\n";
  out += "  r          regenerate\n  p          pin\n  s          read aloud\n  e          edit (your messages)\n  \xe2\x8c\xa5\xe2\x86\x91 / \xe2\x8c\xa5\xe2\x86\x93    move even from the composer\n";
  return out;
}

}  // namespace keymap
