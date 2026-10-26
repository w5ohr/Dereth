// Menu-bar-only mode in the app (#122): the switches in Settings > Mac
// integration, applied through the native layer, and the login item.
#include "app.h"
#include "core/menubar.h"
#include "core/settings.h"
#include "native.h"
#include "ui/widgets.h"

namespace app {

void App::setMenuBarOnly(bool on) {
  menubar::Prefs p = menubar::fromJson(settings().extra);
  p.menuBarOnly = on;
  settings().extra = menubar::withPrefs(settings().extra, p);
  if (on) {
    settings().extra["menuBarExtra"] = true;
    native::setStatusItemVisible(true);
  }
  settings().save();
  native::setMenuBarOnly(on);
  widgets::toastInfo(on ? "Menu-bar only: no Dock icon; closing the window keeps the app in the menu bar" : "Back to a regular app with a Dock icon");
}

void App::drawMenuBarOnlySection(float width) {
  (void)width;
  menubar::Prefs p = menubar::fromJson(settings().extra);
  if (ImGui::Checkbox("Menu-bar only: no Dock icon, and closing the window keeps the app running in the menu bar", &p.menuBarOnly)) setMenuBarOnly(p.menuBarOnly);
  if (ImGui::Checkbox("Open at login, so the hotkey, schedules and the extra are there before any window", &p.openAtLogin)) {
    std::string error;
    if (native::setLoginItem(p.openAtLogin, &error)) {
      settings().extra = menubar::withPrefs(settings().extra, p);
      settings().save();
      widgets::toastInfo(p.openAtLogin ? "Added to your login items" : "Removed from your login items");
    } else {
      widgets::toastError("Login item: " + error);
    }
  }
  widgets::dimText("Login item: %s.", menubar::loginStatusText(native::loginItemStatus()).c_str());
}

}  // namespace app
