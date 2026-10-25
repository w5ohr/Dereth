#include <unistd.h>

#include "ui/emoji_font.h"
#include "ui/theme.h"

#include <vector>

namespace theme {

Fonts fonts;

namespace {

Palette gPalette;
bool gDark = true;
std::map<std::string, ImU32> gOverrides;

ImU32* slotRef(Palette& p, const std::string& id) {
  if (id == "accent") return &p.accent;
  if (id == "accentHover") return &p.accentHover;
  if (id == "bg") return &p.bg;
  if (id == "panel") return &p.panel;
  if (id == "panelAlt") return &p.panelAlt;
  if (id == "border") return &p.border;
  if (id == "text") return &p.text;
  if (id == "textDim") return &p.textDim;
  if (id == "textFaint") return &p.textFaint;
  if (id == "userBubble") return &p.userBubble;
  if (id == "assistantBubble") return &p.assistantBubble;
  if (id == "codeBg") return &p.codeBg;
  if (id == "success") return &p.success;
  if (id == "warning") return &p.warning;
  if (id == "danger") return &p.danger;
  return nullptr;
}

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

struct FontCandidate {
  const char* path;
  int faceIndex;
};

ImFont* loadFirst(const std::vector<FontCandidate>& candidates, float size) {
  ImGuiIO& io = ImGui::GetIO();
  for (const auto& candidate : candidates) {
    ImFontConfig config;
    config.FontNo = candidate.faceIndex;
    config.OversampleH = 2;
    config.OversampleV = 2;
    ImFont* font = io.Fonts->AddFontFromFileTTF(candidate.path, size, &config);
    if (font) return font;
  }
  return io.Fonts->AddFontDefault();
}

}  // namespace

std::string gFontOverride, gFontOverrideBold;

void setFontOverride(const std::string& regularPath, const std::string& boldPath) {
  gFontOverride = regularPath;
  gFontOverrideBold = boldPath;
}

void loadFonts(float baseSize) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();

  std::vector<FontCandidate> uiRegular = {
      {"/System/Library/Fonts/SFNS.ttf", 0},
      {"/System/Library/Fonts/HelveticaNeue.ttc", 0},
      {"/System/Library/Fonts/Supplemental/Arial.ttf", 0},
  };
  std::vector<FontCandidate> uiBold = {
      {"/System/Library/Fonts/HelveticaNeue.ttc", 1},
      {"/System/Library/Fonts/Supplemental/Arial Bold.ttf", 0},
      {"/System/Library/Fonts/SFNS.ttf", 0},
  };
  const std::vector<FontCandidate> monoFace = {
      {"/System/Library/Fonts/SFNSMono.ttf", 0},
      {"/System/Library/Fonts/Menlo.ttc", 0},
      {"/System/Library/Fonts/Supplemental/Courier New.ttf", 0},
  };

  if (!gFontOverride.empty()) {
    uiRegular.insert(uiRegular.begin(), {gFontOverride.c_str(), 0});
    uiBold.insert(uiBold.begin(), {gFontOverrideBold.empty() ? gFontOverride.c_str() : gFontOverrideBold.c_str(), 0});
  }

  // Fallbacks merged into each face, in order: Arial Unicode for arrows,
  // maths, box drawing and CJK; STIX Two Math for the mathematical
  // alphanumerics above U+FFFF; then colour emoji, rasterised by Core Text
  // (the stb rasteriser can't draw Apple Color Emoji's sbix bitmaps).
  const char* kFallback = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf";
  const char* kMath = "/System/Library/Fonts/Supplemental/STIXTwoMath.otf";
  auto mergeFallback = [&] {
    emoji::merge(baseSize, true);   // default-emoji characters (✅ ⭐ ☕ ⏳ ...) before the text fallbacks claim them
    ImFontConfig config;
    config.MergeMode = true;
    config.OversampleH = 2;
    config.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF(kFallback, baseSize, &config);
    if (access(kMath, R_OK) == 0) {
      ImFontConfig math;
      math.MergeMode = true;
      math.OversampleH = 2;
      math.OversampleV = 2;
      io.Fonts->AddFontFromFileTTF(kMath, baseSize, &math);
    }
    emoji::merge(baseSize, false);   // anything emoji-like no text font had
  };

  fonts.regular = loadFirst(uiRegular, baseSize);
  mergeFallback();
  fonts.bold = loadFirst(uiBold, baseSize);
  mergeFallback();
  fonts.mono = loadFirst(monoFace, baseSize);
  mergeFallback();
  io.FontDefault = fonts.regular;
  ImGui::GetStyle().FontSizeBase = baseSize;
}

void pushBold() { ImGui::PushFont(fonts.bold, 0.0f); }
void pushMono() { ImGui::PushFont(fonts.mono, 0.0f); }
void pushScaled(float multiplier) {
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * multiplier);
}
void pop() { ImGui::PopFont(); }

void apply(const std::string& name) {
  gDark = (name != "light");
  ImGuiStyle& style = ImGui::GetStyle();
  const float fontSizeBase = style.FontSizeBase;
  const float fontScaleMain = style.FontScaleMain;
  style = ImGuiStyle();
  style.FontSizeBase = fontSizeBase > 0.0f ? fontSizeBase : 15.0f;
  style.FontScaleMain = fontScaleMain > 0.0f ? fontScaleMain : 1.0f;

  style.WindowRounding = 10.0f;
  style.ChildRounding = 10.0f;
  style.FrameRounding = 8.0f;
  style.PopupRounding = 10.0f;
  style.ScrollbarRounding = 10.0f;
  style.GrabRounding = 8.0f;
  style.TabRounding = 8.0f;
  style.WindowPadding = ImVec2(14, 12);
  style.FramePadding = ImVec2(10, 7);
  style.ItemSpacing = ImVec2(8, 7);
  style.ItemInnerSpacing = ImVec2(6, 5);
  style.ScrollbarSize = 11.0f;
  style.GrabMinSize = 12.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.SeparatorTextBorderSize = 1.0f;
  style.SeparatorTextPadding = ImVec2(16, 6);

  if (gDark) {
    gPalette = Palette{
        /*accent*/ rgb(90, 142, 255),
        /*accentHover*/ rgb(120, 165, 255),
        /*bg*/ rgb(13, 13, 15),
        /*panel*/ rgb(23, 23, 26),
        /*panelAlt*/ rgb(32, 32, 36),
        /*border*/ rgb(48, 48, 54),
        /*text*/ rgb(236, 236, 240),
        /*textDim*/ rgb(166, 166, 176),
        /*textFaint*/ rgb(114, 114, 124),
        /*userBubble*/ rgb(38, 42, 54),
        /*assistantBubble*/ rgb(23, 23, 26),
        /*codeBg*/ rgb(17, 18, 22),
        /*success*/ rgb(76, 194, 125),
        /*warning*/ rgb(226, 170, 72),
        /*danger*/ rgb(234, 96, 96),
    };
  } else {
    gPalette = Palette{
        rgb(42, 104, 232),  rgb(28, 88, 214),   rgb(250, 250, 250), rgb(255, 255, 255),
        rgb(241, 241, 244), rgb(220, 220, 226), rgb(26, 26, 30),    rgb(92, 92, 102),
        rgb(140, 140, 150), rgb(234, 240, 255), rgb(255, 255, 255), rgb(244, 244, 248),
        rgb(32, 150, 90),   rgb(190, 130, 20),  rgb(205, 60, 60),
    };
  }

  if (name == "midnight") {
    // True black for OLED panels and dark rooms; everything else as dark.
    gPalette.bg = rgb(0, 0, 0);
    gPalette.panel = rgb(10, 10, 12);
    gPalette.panelAlt = rgb(20, 20, 24);
    gPalette.border = rgb(34, 34, 40);
    gPalette.codeBg = rgb(8, 8, 10);
    gPalette.userBubble = rgb(18, 22, 32);
    gPalette.assistantBubble = rgb(0, 0, 0);
  }

  for (const auto& [slot, value] : gOverrides) {
    if (ImU32* ref = slotRef(gPalette, slot)) *ref = value;
  }

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = toVec4(gPalette.text);
  colors[ImGuiCol_TextDisabled] = toVec4(gPalette.textFaint);
  colors[ImGuiCol_WindowBg] = toVec4(gPalette.bg);
  colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_PopupBg] = toVec4(gPalette.panel);
  colors[ImGuiCol_Border] = toVec4(gPalette.border);
  colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_FrameBg] = toVec4(gPalette.panelAlt);
  colors[ImGuiCol_FrameBgHovered] = toVec4(gPalette.border);
  colors[ImGuiCol_FrameBgActive] = toVec4(gPalette.border);
  colors[ImGuiCol_TitleBg] = toVec4(gPalette.panel);
  colors[ImGuiCol_TitleBgActive] = toVec4(gPalette.panel);
  colors[ImGuiCol_TitleBgCollapsed] = toVec4(gPalette.panel);
  colors[ImGuiCol_MenuBarBg] = toVec4(gPalette.panel);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ScrollbarGrab] = toVec4(gPalette.border);
  colors[ImGuiCol_ScrollbarGrabHovered] = toVec4(gPalette.textFaint);
  colors[ImGuiCol_ScrollbarGrabActive] = toVec4(gPalette.textDim);
  colors[ImGuiCol_CheckMark] = toVec4(gPalette.accent);
  colors[ImGuiCol_SliderGrab] = toVec4(gPalette.accent);
  colors[ImGuiCol_SliderGrabActive] = toVec4(gPalette.accentHover);
  colors[ImGuiCol_Button] = toVec4(gPalette.panelAlt);
  colors[ImGuiCol_ButtonHovered] = toVec4(gPalette.border);
  colors[ImGuiCol_ButtonActive] = toVec4(gPalette.accent);
  colors[ImGuiCol_Header] = toVec4(gPalette.panelAlt);
  colors[ImGuiCol_HeaderHovered] = toVec4(gPalette.border);
  colors[ImGuiCol_HeaderActive] = toVec4(gPalette.border);
  colors[ImGuiCol_Separator] = toVec4(gPalette.border);
  colors[ImGuiCol_SeparatorHovered] = toVec4(gPalette.accent);
  colors[ImGuiCol_SeparatorActive] = toVec4(gPalette.accent);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ResizeGripHovered] = toVec4(gPalette.border);
  colors[ImGuiCol_ResizeGripActive] = toVec4(gPalette.accent);
  colors[ImGuiCol_Tab] = toVec4(gPalette.panel);
  colors[ImGuiCol_TabHovered] = toVec4(gPalette.panelAlt);
  colors[ImGuiCol_TabSelected] = toVec4(gPalette.panelAlt);
  colors[ImGuiCol_TabSelectedOverline] = toVec4(gPalette.accent);
  colors[ImGuiCol_TabDimmed] = toVec4(gPalette.panel);
  colors[ImGuiCol_TabDimmedSelected] = toVec4(gPalette.panel);
  colors[ImGuiCol_DockingPreview] = toVec4(gPalette.accent);
  colors[ImGuiCol_DockingEmptyBg] = toVec4(gPalette.bg);
  colors[ImGuiCol_TableHeaderBg] = toVec4(gPalette.panel);
  colors[ImGuiCol_TableBorderStrong] = toVec4(gPalette.border);
  colors[ImGuiCol_TableBorderLight] = toVec4(gPalette.border);
  colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt] = toVec4(gPalette.panel);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(toVec4(gPalette.accent).x, toVec4(gPalette.accent).y,
                                           toVec4(gPalette.accent).z, 0.35f);
  colors[ImGuiCol_NavCursor] = toVec4(gPalette.accent);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, gDark ? 0.65f : 0.35f);
}

void setOverrides(const std::map<std::string, ImU32>& overrides) { gOverrides = overrides; }

const Palette& palette() { return gPalette; }
bool isDark() { return gDark; }

ImVec4 toVec4(ImU32 color) {
  return ImVec4(((color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                ((color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                ((color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

}  // namespace theme
