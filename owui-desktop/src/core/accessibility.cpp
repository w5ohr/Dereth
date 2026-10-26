#include "accessibility.h"

#include <algorithm>

#include "common.h"

namespace accessibility {
namespace {

const std::vector<std::string>& recommendedNames() {
  static const std::vector<std::string> v{"opendyslexic", "atkinson hyperlegible", "lexend", "verdana", "tahoma", "trebuchet ms", "comic sans ms", "arial rounded"};
  return v;
}

bool isVariant(const std::string& lowerStem) {
  for (const char* v : {" bold", " italic", " oblique", "-bold", "-italic", "-light", "-medium", "-semibold", "-black", "-thin", "-extra", "bolditalic", " black", " light", " medium", " semibold", " condensed", " narrow"}) {
    if (lowerStem.find(v) != std::string::npos) return true;
  }
  return false;
}

std::string stemOf(const std::string& fileName) {
  std::string name = fileName;
  const size_t slash = name.rfind('/');
  if (slash != std::string::npos) name = name.substr(slash + 1);
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

}  // namespace

Prefs fromJson(const Json& extra) {
  Prefs p;
  const Json j = extra.is_object() ? extra.value("accessibility", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.reducedMotion = j.value("reducedMotion", false);
  p.highContrast = j.value("highContrast", false);
  if (j.contains("uiFont") && j["uiFont"].is_string()) p.uiFont = j["uiFont"].get<std::string>();
  if (j.contains("uiFontBold") && j["uiFontBold"].is_string()) p.uiFontBold = j["uiFontBold"].get<std::string>();
  return p;
}

Json toJson(const Prefs& p) { return Json{{"reducedMotion", p.reducedMotion}, {"highContrast", p.highContrast}, {"uiFont", p.uiFont}, {"uiFontBold", p.uiFontBold}}; }

std::string displayName(const std::string& fileName) {
  std::string stem = stemOf(fileName);
  for (const char* suffix : {"-Regular", "-regular", " Regular", "Regular", "-Book"}) {
    if (core::endsWith(stem, suffix) && stem.size() > std::string(suffix).size()) stem = stem.substr(0, stem.size() - std::string(suffix).size());
  }
  while (!stem.empty() && (stem.back() == '-' || stem.back() == ' ')) stem.pop_back();
  return stem;
}

std::vector<FontChoice> fontChoices(const std::vector<std::string>& files) {
  std::vector<std::string> usable;
  for (const std::string& f : files) {
    const std::string lower = core::lower(f);
    if (core::endsWith(lower, ".ttf") || core::endsWith(lower, ".otf")) usable.push_back(f);
  }
  std::vector<FontChoice> out;
  for (const std::string& f : usable) {
    const std::string stem = core::lower(stemOf(f));
    if (isVariant(stem)) continue;
    FontChoice c;
    c.name = displayName(f);
    c.path = f;
    const std::string lowerName = core::lower(c.name);
    for (const std::string& other : usable) {
      const std::string otherStem = core::lower(stemOf(other));
      if (otherStem == lowerName + " bold" || otherStem == lowerName + "-bold" || otherStem == core::lower(stemOf(f)) + " bold" || otherStem == core::lower(stemOf(f)) + "-bold") {
        c.boldPath = other;
        break;
      }
    }
    for (const std::string& r : recommendedNames()) c.recommended = c.recommended || lowerName.find(r) != std::string::npos;
    bool dup = false;
    for (const FontChoice& e : out) dup = dup || core::lower(e.name) == lowerName;
    if (!dup) out.push_back(c);
  }
  std::stable_sort(out.begin(), out.end(), [](const FontChoice& a, const FontChoice& b) {
    if (a.recommended != b.recommended) return a.recommended;
    return core::lower(a.name) < core::lower(b.name);
  });
  return out;
}

std::map<std::string, std::string> highContrastOverrides(bool dark) {
  if (dark) {
    return {{"bg", "#000000"},      {"panel", "#0a0a0a"},     {"panelAlt", "#161616"}, {"border", "#9a9a9a"},  {"text", "#ffffff"},   {"textDim", "#e0e0e0"},
            {"textFaint", "#b8b8b8"}, {"accent", "#7fb0ff"}, {"accentHover", "#a7c9ff"}, {"userBubble", "#1a2238"}, {"assistantBubble", "#0a0a0a"}, {"codeBg", "#050505"},
            {"success", "#4ee07f"},  {"warning", "#ffc857"},  {"danger", "#ff6b6b"}};
  }
  return {{"bg", "#ffffff"},      {"panel", "#ffffff"},     {"panelAlt", "#f0f0f0"}, {"border", "#5a5a5a"},  {"text", "#000000"},   {"textDim", "#1f1f1f"},
          {"textFaint", "#3a3a3a"}, {"accent", "#0040c0"}, {"accentHover", "#002f8f"}, {"userBubble", "#dde7ff"}, {"assistantBubble", "#ffffff"}, {"codeBg", "#f4f4f4"},
          {"success", "#0a6b2e"},  {"warning", "#7a4f00"},  {"danger", "#b00020"}};
}

}  // namespace accessibility
