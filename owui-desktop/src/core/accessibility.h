// Accessibility options (#143): reduced motion, a high-contrast colour set,
// a UI font picked from the fonts on this Mac (dyslexia-friendly ones
// first when installed) and the UI scale. The pure parts: preferences,
// the font list and the contrast overrides. App-free; --a11ytest.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace accessibility {

using Json = nlohmann::json;

struct Prefs {
  bool reducedMotion = false;   // toasts don't fade, spinners don't spin
  bool highContrast = false;    // stronger text, borders and accent laid over the theme
  std::string uiFont;           // path of a .ttf/.otf for the UI face; "" = the system font
  std::string uiFontBold;       // matching bold face when one exists
};
Prefs fromJson(const Json& extra);   // extra["accessibility"]
Json toJson(const Prefs& prefs);

struct FontChoice {
  std::string name, path, boldPath;
  bool recommended = false;   // a face known to read well for dyslexic or low-vision readers
};
// From the file names found in the font folders: regular faces only, bold
// partners paired, recommended faces first, then alphabetical.
std::vector<FontChoice> fontChoices(const std::vector<std::string>& files);
std::string displayName(const std::string& fileName);   // "Comic Sans MS.ttf" -> "Comic Sans MS", "OpenDyslexic-Regular.otf" -> "OpenDyslexic"

// Theme slot overrides (slot id -> hex) for a high-contrast look on a dark or light base.
std::map<std::string, std::string> highContrastOverrides(bool dark);

}  // namespace accessibility
