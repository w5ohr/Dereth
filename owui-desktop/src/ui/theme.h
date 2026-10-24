// Colors and fonts. Themes mirror Open WebUI's dark/light palettes.
#pragma once

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

// Font/size helpers for the 1.92+ dynamic font API.
void pushBold();
void pushMono();
void pushScaled(float multiplier);   // relative to style.FontSizeBase
void pop();
void apply(const std::string& name);
const Palette& palette();
bool isDark();

ImVec4 toVec4(ImU32 color);

}  // namespace theme
