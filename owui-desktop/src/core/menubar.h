// Menu-bar-only mode (#122): no Dock icon, the app lives in the menu bar
// extra, closing the window keeps it running, and an "open at login" item
// so the hotkey and schedules work with no window. The pure parts: the
// preferences and the login-item status words. App-free; --menubartest.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace menubar {

using Json = nlohmann::json;

struct Prefs {
  bool menuBarOnly = false;   // accessory activation policy: no Dock icon, no app switcher entry
  bool openAtLogin = false;   // registered as a login item through SMAppService
};
Prefs fromJson(const Json& extra);   // extra["menuBarOnly"], extra["openAtLogin"]
Json withPrefs(Json extra, const Prefs& prefs);

// Menu-bar-only needs the menu bar extra, or there would be no way back in.
bool extraRequired(const Prefs& prefs);
bool quitOnLastWindowClosed(const Prefs& prefs);

// SMAppService status codes: 0 not registered, 1 enabled, 2 requires approval, 3 not found.
std::string loginStatusText(int status);

}  // namespace menubar
