// Theme editor in the app (#68): colour overrides per theme applied live,
// VS Code / Base16 import, per-server accent, export; the Settings card
// under Appearance.
#include <map>

#include "app.h"
#include "core/common.h"
#include "core/settings.h"
#include "core/themeedit.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

ImU32 toU32(const themeedit::Colour& c) { return IM_COL32(c.r, c.g, c.b, c.a); }
themeedit::Colour fromU32(ImU32 v) {
  return themeedit::Colour{static_cast<unsigned char>(v & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF), static_cast<unsigned char>((v >> 16) & 0xFF), static_cast<unsigned char>((v >> 24) & 0xFF)};
}

}  // namespace

std::string App::effectiveThemeName() const {
  std::string name = core::Settings::instance().theme;
  if (name == "system") name = native::systemIsDark() ? "dark" : "light";
  return name;
}

std::map<std::string, ImU32> App::themeOverridesForCurrent() const {
  std::map<std::string, ImU32> out;
  const std::string name = effectiveThemeName();
  const Json& extra = core::Settings::instance().extra;
  for (const auto& [slot, hex] : themeedit::overridesFromJson(extra, name)) {
    themeedit::Colour c;
    if (themeedit::parseHex(hex, &c)) out[slot] = toU32(c);
  }
  const std::string accent = themeedit::serverAccent(extra, api_.server());
  themeedit::Colour a;
  if (!accent.empty() && themeedit::parseHex(accent, &a)) {
    out["accent"] = toU32(a);
    out["accentHover"] = toU32(themeedit::hoverFor(a, name != "light"));
  }
  return out;
}

void App::drawThemeEditorSection(float width) {
  card("themeeditor", [&] {
    const std::string name = effectiveThemeName();
    themeedit::Overrides overrides = themeedit::overridesFromJson(settings().extra, name);
    ImGui::PushTextWrapPos(width - 20.0f);
    widgets::dimText("Colours for the %s theme. Changes apply as you drag; each theme keeps its own set. A server accent (below) overrides the accent while that server is active.", name.c_str());
    ImGui::PopTextWrapPos();
    bool changed = false;
    const theme::Palette& pal = theme::palette();
    for (const themeedit::Slot& s : themeedit::slots()) {
      ImGui::PushID(s.id);
      ImU32 current = 0;
      const std::string id = s.id;
      if (id == "accent") current = pal.accent;
      else if (id == "accentHover") current = pal.accentHover;
      else if (id == "bg") current = pal.bg;
      else if (id == "panel") current = pal.panel;
      else if (id == "panelAlt") current = pal.panelAlt;
      else if (id == "border") current = pal.border;
      else if (id == "text") current = pal.text;
      else if (id == "textDim") current = pal.textDim;
      else if (id == "textFaint") current = pal.textFaint;
      else if (id == "userBubble") current = pal.userBubble;
      else if (id == "assistantBubble") current = pal.assistantBubble;
      else if (id == "codeBg") current = pal.codeBg;
      else if (id == "success") current = pal.success;
      else if (id == "warning") current = pal.warning;
      else if (id == "danger") current = pal.danger;
      ImVec4 v = theme::toVec4(current);
      float rgb[3] = {v.x, v.y, v.z};
      if (ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        themeedit::Colour c{static_cast<unsigned char>(rgb[0] * 255.0f + 0.5f), static_cast<unsigned char>(rgb[1] * 255.0f + 0.5f), static_cast<unsigned char>(rgb[2] * 255.0f + 0.5f), 255};
        overrides[id] = themeedit::toHex(c);
        changed = true;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(s.label);
      ImGui::SameLine();
      widgets::dimText("%s", themeedit::toHex(fromU32(current)).c_str());
      if (overrides.count(id)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
          overrides.erase(id);
          changed = true;
        }
      }
      ImGui::PopID();
    }
    if (ImGui::SmallButton("Reset all")) {
      overrides.clear();
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Import VS Code / Base16 theme...")) {
      const std::vector<std::string> picked = platform::openFiles(false);
      if (!picked.empty()) {
        bool ok = false;
        const std::string text = core::readFile(picked[0], &ok);
        std::string themeName, error;
        const themeedit::Overrides imported = ok ? themeedit::importTheme(text, &themeName, &error) : themeedit::Overrides{};
        if (imported.empty()) {
          widgets::toastError(ok ? error : "Could not read " + picked[0]);
        } else {
          overrides = imported;
          changed = true;
          widgets::toast(widgets::ToastLevel::Success, "Imported " + (themeName.empty() ? std::string("the theme") : themeName) + " (" + std::to_string(imported.size()) + " colours) into " + name);
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy as JSON")) {
      platform::setClipboardText(themeedit::exportJson(overrides, name));
      widgets::toastInfo("Copied the " + name + " overrides");
    }
    if (changed) {
      settings().extra = themeedit::withOverrides(settings().extra, name, overrides);
      settings().save();
      applyTheme();
    }

    ImGui::Dummy(ImVec2(0, 6));
    theme::pushBold();
    ImGui::TextUnformatted("Server accent");
    theme::pop();
    const std::string server = api_.server();
    const std::string accent = themeedit::serverAccent(settings().extra, server);
    themeedit::Colour ac{};
    if (accent.empty() || !themeedit::parseHex(accent, &ac)) ac = fromU32(pal.accent);
    ImVec4 av = theme::toVec4(toU32(ac));
    float argb[3] = {av.x, av.y, av.z};
    ImGui::PushID("serveraccent");
    if (ImGui::ColorEdit3("##sa", argb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
      themeedit::Colour c{static_cast<unsigned char>(argb[0] * 255.0f + 0.5f), static_cast<unsigned char>(argb[1] * 255.0f + 0.5f), static_cast<unsigned char>(argb[2] * 255.0f + 0.5f), 255};
      settings().extra = themeedit::withServerAccent(settings().extra, server, themeedit::toHex(c));
      settings().save();
      applyTheme();
    }
    ImGui::SameLine();
    ImGui::Text("%s", server.c_str());
    ImGui::SameLine();
    widgets::dimText("%s", accent.empty() ? "(theme accent)" : accent.c_str());
    if (!accent.empty()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) {
        settings().extra = themeedit::withServerAccent(settings().extra, server, "");
        settings().save();
        applyTheme();
      }
    }
    ImGui::PopID();
    widgets::dimText("Give each server its own accent so the header, buttons and highlights tell you at a glance which instance you are on.");
  }, width);
}

}  // namespace app
