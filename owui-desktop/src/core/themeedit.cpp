#include "themeedit.h"

#include <algorithm>
#include <cmath>

#include "common.h"

namespace themeedit {
namespace {

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string pick(const std::map<std::string, std::string>& colours, std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    auto it = colours.find(k);
    if (it != colours.end()) {
      Colour c;
      if (parseHex(it->second, &c)) return toHex(c);
    }
  }
  return "";
}

}  // namespace

bool parseHex(const std::string& textIn, Colour* out) {
  std::string t = core::trim(textIn);
  if (!t.empty() && t[0] == '#') t.erase(0, 1);
  if (t.size() == 3) t = std::string{t[0], t[0], t[1], t[1], t[2], t[2]};
  if (t.size() != 6 && t.size() != 8) return false;
  unsigned char v[4] = {0, 0, 0, 255};
  for (size_t i = 0; i < t.size() / 2; ++i) {
    const int hi = hexVal(t[i * 2]), lo = hexVal(t[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    v[i] = static_cast<unsigned char>(hi * 16 + lo);
  }
  *out = Colour{v[0], v[1], v[2], v[3]};
  return true;
}

std::string toHex(const Colour& c) {
  char buf[12];
  if (c.a == 255) snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
  else snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
  return buf;
}

Colour lighten(const Colour& c, float amount) {
  auto mix = [&](unsigned char v) {
    const float target = amount >= 0 ? 255.0f : 0.0f;
    const float f = std::fabs(amount);
    return static_cast<unsigned char>(std::lround(v + (target - v) * std::clamp(f, 0.0f, 1.0f)));
  };
  return Colour{mix(c.r), mix(c.g), mix(c.b), c.a};
}

Colour hoverFor(const Colour& accent, bool dark) { return lighten(accent, dark ? 0.18f : -0.15f); }

const std::vector<Slot>& slots() {
  static const std::vector<Slot> v{{"accent", "Accent"},         {"accentHover", "Accent (hover)"}, {"bg", "Background"},          {"panel", "Panels"},
                                   {"panelAlt", "Fields"},       {"border", "Borders"},             {"text", "Text"},               {"textDim", "Secondary text"},
                                   {"textFaint", "Faint text"},  {"userBubble", "Your messages"},   {"assistantBubble", "Replies"}, {"codeBg", "Code blocks"},
                                   {"success", "Success"},       {"warning", "Warning"},            {"danger", "Danger"}};
  return v;
}

Overrides overridesFromJson(const Json& extra, const std::string& themeName) {
  Overrides out;
  if (!extra.is_object()) return out;
  const Json all = extra.value("themeOverrides", Json::object());
  if (!all.is_object()) return out;
  const Json mine = all.value(themeName, Json::object());
  if (!mine.is_object()) return out;
  for (const Slot& s : slots()) {
    if (!mine.contains(s.id) || !mine[s.id].is_string()) continue;
    Colour c;
    if (parseHex(mine[s.id].get<std::string>(), &c)) out[s.id] = toHex(c);
  }
  return out;
}

Json withOverrides(Json extra, const std::string& themeName, const Overrides& overrides) {
  if (!extra.is_object()) extra = Json::object();
  Json& all = extra["themeOverrides"];
  if (!all.is_object()) all = Json::object();
  if (overrides.empty()) all.erase(themeName);
  else all[themeName] = Json(overrides);
  if (all.empty()) extra.erase("themeOverrides");
  return extra;
}

std::string serverAccent(const Json& extra, const std::string& server) {
  if (!extra.is_object() || server.empty()) return "";
  const Json accents = extra.value("serverAccents", Json::object());
  if (!accents.is_object() || !accents.contains(server) || !accents[server].is_string()) return "";
  Colour c;
  return parseHex(accents[server].get<std::string>(), &c) ? toHex(c) : "";
}

Json withServerAccent(Json extra, const std::string& server, const std::string& hex) {
  if (!extra.is_object()) extra = Json::object();
  Json& accents = extra["serverAccents"];
  if (!accents.is_object()) accents = Json::object();
  Colour c;
  if (hex.empty() || !parseHex(hex, &c)) accents.erase(server);
  else accents[server] = toHex(c);
  if (accents.empty()) extra.erase("serverAccents");
  return extra;
}

std::string stripJsonc(const std::string& text) {
  std::string out;
  bool inString = false, lineComment = false, blockComment = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const char n = i + 1 < text.size() ? text[i + 1] : '\0';
    if (lineComment) {
      if (c == '\n') {
        lineComment = false;
        out += c;
      }
      continue;
    }
    if (blockComment) {
      if (c == '*' && n == '/') {
        blockComment = false;
        ++i;
      }
      continue;
    }
    if (inString) {
      out += c;
      if (c == '\\' && i + 1 < text.size()) out += text[++i];
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') {
      inString = true;
      out += c;
    } else if (c == '/' && n == '/') {
      lineComment = true;
    } else if (c == '/' && n == '*') {
      blockComment = true;
      ++i;
    } else {
      out += c;
    }
  }
  // Trailing commas before } or ].
  std::string clean;
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == ',') {
      size_t j = i + 1;
      while (j < out.size() && isspace(static_cast<unsigned char>(out[j]))) ++j;
      if (j < out.size() && (out[j] == '}' || out[j] == ']')) continue;
    }
    clean += out[i];
  }
  return clean;
}

Overrides importTheme(const std::string& text, std::string* name, std::string* error) {
  Overrides out;
  if (name) name->clear();
  Json j = Json::parse(stripJsonc(text), nullptr, false);
  std::map<std::string, std::string> colours;
  if (j.is_object() && j.contains("colors") && j["colors"].is_object()) {
    // VS Code.
    for (const auto& [k, v] : j["colors"].items()) {
      if (v.is_string()) colours[k] = v.get<std::string>();
    }
    if (name) *name = j.value("name", "");
    const std::string bg = pick(colours, {"editor.background"});
    out["accent"] = pick(colours, {"button.background", "focusBorder", "textLink.foreground", "activityBarBadge.background"});
    out["accentHover"] = pick(colours, {"button.hoverBackground"});
    out["bg"] = bg;
    out["panel"] = pick(colours, {"sideBar.background", "activityBar.background", "editorWidget.background"});
    out["panelAlt"] = pick(colours, {"input.background", "editorWidget.background", "dropdown.background"});
    out["border"] = pick(colours, {"panel.border", "sideBar.border", "editorGroup.border", "widget.border"});
    out["text"] = pick(colours, {"editor.foreground", "foreground"});
    out["textDim"] = pick(colours, {"descriptionForeground", "sideBar.foreground", "tab.inactiveForeground"});
    out["textFaint"] = pick(colours, {"disabledForeground", "editorLineNumber.foreground"});
    out["userBubble"] = pick(colours, {"editor.selectionBackground", "list.activeSelectionBackground"});
    out["assistantBubble"] = pick(colours, {"sideBar.background", "editor.background"});
    out["codeBg"] = pick(colours, {"textCodeBlock.background", "terminal.background", "editor.background"});
    out["success"] = pick(colours, {"terminal.ansiGreen", "gitDecoration.addedResourceForeground", "testing.iconPassed"});
    out["warning"] = pick(colours, {"terminal.ansiYellow", "editorWarning.foreground", "list.warningForeground"});
    out["danger"] = pick(colours, {"errorForeground", "terminal.ansiRed", "editorError.foreground"});
  } else {
    // Base16: YAML ("base00: \"181818\"") or JSON with the same keys.
    if (j.is_object()) {
      for (const auto& [k, v] : j.items()) {
        if (v.is_string()) colours[core::lower(k)] = v.get<std::string>();
      }
    } else {
      for (const std::string& raw : core::split(text, '\n')) {
        const std::string line = core::trim(raw);
        const size_t colon = line.find(':');
        if (colon == std::string::npos || line[0] == '#') continue;
        std::string key = core::lower(core::trim(line.substr(0, colon)));
        std::string value = core::trim(line.substr(colon + 1));
        if (value.size() >= 2 && (value[0] == '"' || value[0] == '\'')) value = value.substr(1, value.size() - 2);
        colours[key] = value;
      }
    }
    if (!colours.count("base00") || !colours.count("base05")) {
      if (error) *error = "Not a VS Code theme (no \"colors\") and not a Base16 scheme (no base00..base0F)";
      return {};
    }
    if (name) *name = colours.count("scheme") ? colours["scheme"] : colours.count("name") ? colours["name"] : "";
    out["bg"] = pick(colours, {"base00"});
    out["panel"] = pick(colours, {"base01"});
    out["panelAlt"] = pick(colours, {"base02"});
    out["border"] = pick(colours, {"base03"});
    out["text"] = pick(colours, {"base05"});
    out["textDim"] = pick(colours, {"base04"});
    out["textFaint"] = pick(colours, {"base03"});
    out["userBubble"] = pick(colours, {"base02"});
    out["assistantBubble"] = pick(colours, {"base01"});
    out["codeBg"] = pick(colours, {"base01"});
    out["accent"] = pick(colours, {"base0d"});
    out["success"] = pick(colours, {"base0b"});
    out["warning"] = pick(colours, {"base0a"});
    out["danger"] = pick(colours, {"base08"});
  }
  for (auto it = out.begin(); it != out.end();) {
    if (it->second.empty()) it = out.erase(it);
    else ++it;
  }
  if (out.count("accent") && !out.count("accentHover")) {
    Colour a;
    parseHex(out["accent"], &a);
    Colour bgc;
    const bool dark = !out.count("bg") || !parseHex(out["bg"], &bgc) || (bgc.r + bgc.g + bgc.b) < 384;
    out["accentHover"] = toHex(hoverFor(a, dark));
  }
  if (out.empty() && error) *error = "No usable colours found";
  return out;
}

std::string exportJson(const Overrides& overrides, const std::string& themeName) {
  return Json{{"theme", themeName}, {"colors", Json(overrides)}}.dump(2);
}

}  // namespace themeedit
