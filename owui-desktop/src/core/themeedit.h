// Theme editor (#68): colour overrides per theme (dark / light / midnight),
// VS Code and Base16 theme import, per-server accent colours. The pure
// parts: hex colours, the slot table, overrides as JSON, the importers and
// the export. App-free; --themetest covers it.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace themeedit {

using Json = nlohmann::json;

struct Colour {
  unsigned char r = 0, g = 0, b = 0, a = 255;
};
bool parseHex(const std::string& text, Colour* out);   // #rgb, #rrggbb, #rrggbbaa, with or without '#'
std::string toHex(const Colour& c);                     // "#rrggbb" ("#rrggbbaa" when alpha < 255)
Colour lighten(const Colour& c, float amount);          // amount > 0 towards white, < 0 towards black
Colour hoverFor(const Colour& accent, bool dark);       // the accent's hover shade

struct Slot {
  const char* id;
  const char* label;
};
const std::vector<Slot>& slots();   // the palette's fifteen colours

using Overrides = std::map<std::string, std::string>;   // slot id -> hex
Overrides overridesFromJson(const Json& extra, const std::string& themeName);   // extra["themeOverrides"][theme]
Json withOverrides(Json extra, const std::string& themeName, const Overrides& overrides);   // returns the updated extra
std::string serverAccent(const Json& extra, const std::string& server);   // extra["serverAccents"][server], "" when none
Json withServerAccent(Json extra, const std::string& server, const std::string& hex);   // "" clears

// A VS Code theme (JSON, comments and trailing commas allowed) or a Base16
// scheme (YAML or JSON). Returns the slots the file could fill.
Overrides importTheme(const std::string& text, std::string* name, std::string* error);
std::string stripJsonc(const std::string& text);
std::string exportJson(const Overrides& overrides, const std::string& themeName);

}  // namespace themeedit
