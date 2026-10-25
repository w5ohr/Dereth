// `Open WebUI --menubartest`: offline checks of menu-bar-only mode.
#include <cstdio>
#include <string>

#include "core/menubar.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runMenuBarSelfTest() {
  using namespace menubar;
  printf("Open WebUI desktop menu-bar-only self-test\n\n");
  const Prefs d = fromJson(Json::object());
  report("defaults: regular app, not a login item", !d.menuBarOnly && !d.openAtLogin && !fromJson(Json("x")).menuBarOnly);
  Prefs p;
  p.menuBarOnly = true;
  p.openAtLogin = true;
  const Json extra = withPrefs(Json{{"other", 1}}, p);
  report("withPrefs keeps other keys and round-trips", extra["other"] == 1 && fromJson(extra).menuBarOnly && fromJson(extra).openAtLogin && fromJson(withPrefs(Json(), d)).menuBarOnly == false);
  report("rules: menu-bar-only needs the extra and keeps running after the last window closes", extraRequired(p) && !extraRequired(d) && !quitOnLastWindowClosed(p) && quitOnLastWindowClosed(d));
  report("loginStatusText", loginStatusText(1) == "opens at login" && loginStatusText(2).find("approval") != std::string::npos && loginStatusText(0).find("not set") == 0 && loginStatusText(3).find("not found") != std::string::npos &&
                                loginStatusText(99).find("not set") == 0);
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
