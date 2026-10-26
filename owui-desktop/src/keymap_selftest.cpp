// `Open WebUI --keymaptest`: offline checks of the shortcut editor.
#include <cstdio>
#include <string>

#include "core/keymap.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runKeymapSelfTest() {
  using namespace keymap;
  printf("Open WebUI desktop shortcut self-test\n\n");

  Chord c;
  report("parse: names in any case and order, aliases, symbols", parse("Cmd+Shift+K", &c) && c.cmd && c.shift && !c.alt && c.key == "k" && parse("shift+cmd+k", &c) && format(c) == "shift+cmd+k" &&
                                                                     parse("ctrl+alt+Return", &c) && c.key == "enter" && format(c) == "ctrl+alt+enter" && parse("\xe2\x8c\x98\xe2\x87\xa7" "L", &c) && c.cmd && c.shift && c.key == "l" &&
                                                                     parse("cmd+-", &c) && c.key == "-" && parse("cmd+/", &c) && c.key == "/" && parse("f5", &c) && c.key == "f5" && !c.cmd);
  report("parse: rejects junk", !parse("", &c) && !parse("cmd+", &c) && !parse("bogus+k", &c) && !parse("cmd+ñ", &c) && !parse("cmd+f13", &c));
  parse("cmd+shift+k", &c);
  report("pretty / format", pretty(c) == "\xe2\x87\xa7\xe2\x8c\x98" "K" && format(c) == "shift+cmd+k" && [] { Chord e; return pretty(e) == "-" && format(e).empty(); }() &&
                                [] { Chord u; parse("cmd+up", &u); return pretty(u) == "\xe2\x8c\x98\xe2\x86\x91"; }());

  std::vector<Shortcut> list = defaults();
  bool allValid = !list.empty();
  for (const Shortcut& s : list) allValid = allValid && parse(s.def, &c) && s.chord == s.def && !s.label.empty();
  report("defaults: every chord parses, no conflicts, known ids", allValid && conflicts(list).empty() && find(list, "palette") && find(list, "palette")->menu && find(list, "chat.refresh") && !find(list, "nope"));
  applyOverrides(list, Json{{"view.sidebar", "Cmd+Shift+B"}, {"chat.refresh", ""}, {"palette", "garbage"}, {"unknown", "cmd+z"}});
  report("applyOverrides: canonical chord, unbinding, bad values and unknown ids ignored", find(list, "view.sidebar")->chord == "shift+cmd+b" && find(list, "chat.refresh")->chord.empty() && find(list, "palette")->chord == "cmd+k");
  const Json ov = overrides(list);
  report("overrides: only changed bindings", ov.size() == 2 && ov["view.sidebar"] == "shift+cmd+b" && ov["chat.refresh"] == "" && !ov.contains("palette"));
  applyOverrides(list, Json{{"chat.refresh", "cmd+n"}});
  const std::vector<Conflict> cs = conflicts(list);
  report("conflicts: shared chords reported once per pair, unbound never conflicts", cs.size() == 1 && cs[0].a == "chat.new" && cs[0].b == "chat.refresh" && cs[0].chord == "cmd+n" &&
                                                                                        [&] { std::vector<Shortcut> l = defaults(); applyOverrides(l, Json{{"chat.new", ""}, {"chat.refresh", ""}}); return conflicts(l).empty(); }());

  std::string key;
  int mods = 0;
  report("menuEquivalent: letters, punctuation, modifier bits, specials, F-keys", parse("cmd+shift+n", &c) && menuEquivalent(c, &key, &mods) && key == "n" && mods == 3 && parse("ctrl+alt+.", &c) &&
                                                                                      menuEquivalent(c, &key, &mods) && key == "." && mods == 12 && parse("cmd+enter", &c) && menuEquivalent(c, &key, &mods) && key == "\r" &&
                                                                                      parse("cmd+f2", &c) && menuEquivalent(c, &key, &mods) && key == "\xef\x9c\x85" && !menuEquivalent(Chord{}, &key, &mods));

  VimState st;
  report("vimStep: j/k, gg within a second, G, lone g does nothing, timeout resets", vimStep(st, "j", false, 0) == VimAction::Down && vimStep(st, "k", false, 0) == VimAction::Up &&
                                                                                        vimStep(st, "g", false, 1.0) == VimAction::None && vimStep(st, "g", false, 1.5) == VimAction::Top &&
                                                                                        vimStep(st, "g", false, 2.0) == VimAction::None && vimStep(st, "g", false, 3.5) == VimAction::None &&
                                                                                        vimStep(st, "g", true, 4.0) == VimAction::Bottom && vimStep(st, "/", false, 5) == VimAction::Search &&
                                                                                        vimStep(st, "y", false, 5) == VimAction::Copy && vimStep(st, "i", false, 5) == VimAction::Compose &&
                                                                                        vimStep(st, "x", false, 5) == VimAction::None && std::string(vimActionName(VimAction::Pin)) == "pin");
  const std::string sheet = cheatSheet(defaults(), true);
  report("cheatSheet: grouped, pretty chords, vim section", sheet.find("General\n") == 0 && sheet.find("\xe2\x8c\x98" "K") != std::string::npos && sheet.find("Command palette") != std::string::npos &&
                                                                sheet.find("gg / G") != std::string::npos && cheatSheet(defaults(), false).find("gg / G") == std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
