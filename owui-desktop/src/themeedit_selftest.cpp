// `Open WebUI --themetest`: offline checks of the theme editor.
#include <cstdio>
#include <string>

#include "core/themeedit.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runThemeSelfTest() {
  using namespace themeedit;
  printf("Open WebUI desktop theme-editor self-test\n\n");

  Colour c;
  report("parseHex: #rrggbb, #rgb, rrggbbaa, no hash, trimmed; junk refused", parseHex("#5a8eff", &c) && c.r == 0x5a && c.g == 0x8e && c.b == 0xff && c.a == 255 && parseHex("#fff", &c) && c.r == 255 && c.b == 255 &&
                                                                                 parseHex("10182080", &c) && c.a == 0x80 && parseHex("  #000000 ", &c) && !parseHex("#12345", &c) && !parseHex("#gg0000", &c) && !parseHex("", &c));
  report("toHex round-trips, alpha only when not opaque", toHex(Colour{0x5a, 0x8e, 0xff, 255}) == "#5a8eff" && toHex(Colour{1, 2, 3, 128}) == "#01020380");
  const Colour l = lighten(Colour{100, 100, 100, 255}, 0.5f);
  const Colour d = lighten(Colour{100, 100, 100, 255}, -0.5f);
  report("lighten / hoverFor", l.r == 178 && d.r == 50 && hoverFor(Colour{0, 0, 0, 255}, true).r > 0 && hoverFor(Colour{200, 200, 200, 255}, false).r < 200);
  report("slots: fifteen with ids and labels", slots().size() == 15 && std::string(slots()[0].id) == "accent" && std::string(slots()[14].id) == "danger");

  Json extra = Json::object();
  extra = withOverrides(extra, "dark", Overrides{{"accent", "#E0453C"}, {"bg", "#101820"}, {"bogus", "#ffffff"}});
  Overrides back = overridesFromJson(extra, "dark");
  report("overrides: stored per theme, canonical hex, unknown slots ignored on read, other themes untouched", back.size() == 2 && back["accent"] == "#e0453c" && overridesFromJson(extra, "light").empty() &&
                                                                                                                 extra["themeOverrides"]["dark"].size() == 3 && withOverrides(extra, "dark", {}).contains("themeOverrides") == false);
  extra = withServerAccent(extra, "http://localhost:3001", "#2FB36B");
  report("server accents", serverAccent(extra, "http://localhost:3001") == "#2fb36b" && serverAccent(extra, "http://other").empty() && !withServerAccent(extra, "http://localhost:3001", "").contains("serverAccents") &&
                               serverAccent(Json(), "x").empty());

  const std::string vscode = R"({
  // Dracula-ish
  "name": "Night Test",
  "type": "dark",
  "colors": {
    "editor.background": "#282a36",
    "editor.foreground": "#f8f8f2",
    "sideBar.background": "#21222c",
    "input.background": "#343746",
    "panel.border": "#191a21",
    "button.background": "#44475a",
    "focusBorder": "#bd93f9",
    "descriptionForeground": "#6272a4",
    "errorForeground": "#ff5555",
    "terminal.ansiGreen": "#50fa7b",
    "terminal.ansiYellow": "#f1fa8c", /* trailing comma next */
  },
  "tokenColors": []
})";
  std::string name, error;
  Overrides v = importTheme(vscode, &name, &error);
  report("importTheme: VS Code JSONC - comments and trailing commas stripped, slots mapped, hover derived", error.empty() && name == "Night Test" && v["bg"] == "#282a36" && v["text"] == "#f8f8f2" && v["panel"] == "#21222c" &&
                                                                                                             v["panelAlt"] == "#343746" && v["border"] == "#191a21" && v["accent"] == "#44475a" && v["textDim"] == "#6272a4" &&
                                                                                                             v["danger"] == "#ff5555" && v["success"] == "#50fa7b" && v["warning"] == "#f1fa8c" && v.count("accentHover") && !v.count("textFaint"),
         error.empty() ? std::to_string(v.size()) + " slots" : error);
  const std::string base16 = "scheme: \"Ocean\"\nauthor: \"someone\"\nbase00: \"2b303b\"\nbase01: \"343d46\"\nbase02: \"4f5b66\"\nbase03: \"65737e\"\nbase04: \"a7adba\"\nbase05: \"c0c5ce\"\nbase08: \"bf616a\"\nbase0A: \"ebcb8b\"\nbase0B: \"a3be8c\"\nbase0D: \"8fa1b3\"\n";
  Overrides b = importTheme(base16, &name, &error);
  report("importTheme: Base16 YAML - scheme name, slots mapped, accent hover derived", error.empty() && name == "Ocean" && b["bg"] == "#2b303b" && b["text"] == "#c0c5ce" && b["accent"] == "#8fa1b3" && b["danger"] == "#bf616a" &&
                                                                                          b["warning"] == "#ebcb8b" && b["success"] == "#a3be8c" && b["textDim"] == "#a7adba" && b.count("accentHover"));
  Overrides bj = importTheme("{\"base00\":\"000000\",\"base05\":\"ffffff\",\"base0D\":\"3366ff\"}", &name, &error);
  report("importTheme: Base16 JSON, and garbage reports an error", bj["bg"] == "#000000" && bj["accent"] == "#3366ff" && importTheme("hello world", &name, &error).empty() && !error.empty());
  report("stripJsonc keeps // inside strings", stripJsonc("{\"url\": \"http://x\", // c\n \"a\": 1,}").find("http://x") != std::string::npos && stripJsonc("{\"a\": 1,}") == "{\"a\": 1}");
  report("exportJson", exportJson(Overrides{{"accent", "#e0453c"}}, "dark").find("\"accent\": \"#e0453c\"") != std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
