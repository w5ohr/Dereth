// Accessibility options in the app (#143): the Settings card and the
// application of reduced motion, high contrast, the UI font and the scale.
#include <dirent.h>

#include <algorithm>

#include "app.h"
#include "core/accessibility.h"
#include "core/common.h"
#include "core/settings.h"
#include "native.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<accessibility::FontChoice> gChoices;
bool gChoicesLoaded = false;

std::vector<std::string> listFonts() {
  std::vector<std::string> files;
  const std::string home = native::homeDirectory();
  for (const std::string& dir : {home + "/Library/Fonts", std::string("/Library/Fonts"), std::string("/System/Library/Fonts/Supplemental")}) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (dirent* e = readdir(d)) {
      if (e->d_name[0] == '.') continue;
      files.push_back(dir + "/" + e->d_name);
    }
    closedir(d);
  }
  return files;
}

}  // namespace

accessibility::Prefs App::accessibilityPrefs() const { return accessibility::fromJson(core::Settings::instance().extra); }

void App::applyAccessibility() {
  const accessibility::Prefs p = accessibilityPrefs();
  widgets::setReducedMotion(p.reducedMotion);
  theme::setFontOverride(p.uiFont, p.uiFontBold);
  ImGui::GetStyle().FontScaleMain = std::clamp(settings().uiScale, 0.8f, 1.6f);
  applyTheme();   // high contrast rides on the theme overrides
}

void App::drawAccessibilitySection(float width) {
  card("accessibility", [&] {
    accessibility::Prefs p = accessibilityPrefs();
    bool changed = false;
    if (ImGui::Checkbox("Reduced motion: no fading toasts or spinning indicators", &p.reducedMotion)) changed = true;
    if (ImGui::Checkbox("High contrast: black-and-white surfaces, strong borders and text on top of the theme", &p.highContrast)) changed = true;
    if (!gChoicesLoaded) {
      gChoicesLoaded = true;
      gChoices = accessibility::fontChoices(listFonts());
    }
    std::string current = "System font (San Francisco)";
    for (const accessibility::FontChoice& c : gChoices) {
      if (c.path == p.uiFont) current = c.name + (c.recommended ? "  (recommended)" : "");
    }
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("UI font", current.c_str())) {
      if (ImGui::Selectable("System font (San Francisco)", p.uiFont.empty())) {
        p.uiFont.clear();
        p.uiFontBold.clear();
        changed = true;
        fontsReloadRequested = true;
      }
      for (const accessibility::FontChoice& c : gChoices) {
        const std::string label = c.name + (c.recommended ? "  (recommended)" : "");
        if (ImGui::Selectable(label.c_str(), c.path == p.uiFont)) {
          p.uiFont = c.path;
          p.uiFontBold = c.boldPath;
          changed = true;
          fontsReloadRequested = true;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) gChoicesLoaded = false;
    widgets::dimText("Fonts from ~/Library/Fonts, /Library/Fonts and the system's extras (%zu found). Install OpenDyslexic, Atkinson Hyperlegible or Lexend and they appear here, recommended.", gChoices.size());
    float scale = std::clamp(settings().uiScale, 0.8f, 1.6f);
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("UI scale", &scale, 0.8f, 1.6f, "%.2fx")) {
      settings().uiScale = scale;
      ImGui::GetStyle().FontScaleMain = scale;
      settings().save();
    }
    if (changed) {
      settings().extra["accessibility"] = accessibility::toJson(p);
      settings().save();
      applyAccessibility();
    }
    widgets::dimText("Text size (above), the theme editor and reading mode combine with these; high contrast overrides theme colours while it is on.");
  }, width);
}

}  // namespace app
