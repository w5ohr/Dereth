#include "menubar.h"

namespace menubar {

Prefs fromJson(const Json& extra) {
  Prefs p;
  if (!extra.is_object()) return p;
  p.menuBarOnly = extra.value("menuBarOnly", false);
  p.openAtLogin = extra.value("openAtLogin", false);
  return p;
}

Json withPrefs(Json extra, const Prefs& p) {
  if (!extra.is_object()) extra = Json::object();
  extra["menuBarOnly"] = p.menuBarOnly;
  extra["openAtLogin"] = p.openAtLogin;
  return extra;
}

bool extraRequired(const Prefs& p) { return p.menuBarOnly; }
bool quitOnLastWindowClosed(const Prefs& p) { return !p.menuBarOnly; }

std::string loginStatusText(int status) {
  switch (status) {
    case 1: return "opens at login";
    case 2: return "waiting for your approval in System Settings > General > Login Items";
    case 3: return "login item not found (the app may have moved)";
    default: return "not set to open at login";
  }
}

}  // namespace menubar
