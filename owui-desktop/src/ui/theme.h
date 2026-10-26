// Colors and fonts. Themes mirror Open WebUI's dark/light palettes.
#pragma once

#include <map>
#include <string>

#include "imgui.h"

namespace theme {

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;
  ImFont* mono = nullptr;
};

struct Palette {
  ImU32 accent;
  ImU32 accentHover;
  ImU32 bg;
  ImU32 panel;
  ImU32 panelAlt;
  ImU32 border;
  ImU32 text;
  ImU32 textDim;
  ImU32 textFaint;
  ImU32 userBubble;
  ImU32 assistantBubble;
  ImU32 codeBg;
  ImU32 success;
  ImU32 warning;
  ImU32 danger;
};

extern Fonts fonts;

void loadFonts(float baseSize);
// A .ttf/.otf to use as the UI face (and its bold partner) instead of the
// system font; "" clears. Takes effect on the next loadFonts().
void setFontOverride(const std::string& regularPath, const std::string& boldPath);

// Font/size helpers for the 1.92+ dynamic font API.
void pushBold();
void pushMono();
void pushScaled(float multiplier);   // relative to style.FontSizeBase
void pop();
void apply(const std::string& name);
// Colour overrides by slot id (accent, bg, panel, ...) laid over the named
// theme on the next apply(); the theme editor sets them.
void setOverrides(const std::map<std::string, ImU32>& overrides);
const Palette& palette();
bool isDark();

ImVec4 toVec4(ImU32 color);

}  // namespace theme
