// `Open WebUI --a11ytest`: offline checks of the accessibility options.
#include <cstdio>
#include <string>

#include "core/accessibility.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runAccessibilitySelfTest() {
  using namespace accessibility;
  printf("Open WebUI desktop accessibility self-test\n\n");

  const Prefs d = fromJson(Json::object());
  report("defaults: motion on, normal contrast, system font", !d.reducedMotion && !d.highContrast && d.uiFont.empty());
  Prefs p;
  p.reducedMotion = true;
  p.highContrast = true;
  p.uiFont = "/x/Tahoma.ttf";
  p.uiFontBold = "/x/Tahoma Bold.ttf";
  const Prefs back = fromJson(Json{{"accessibility", toJson(p)}});
  report("round-trip; junk ignored", back.reducedMotion && back.highContrast && back.uiFont == p.uiFont && back.uiFontBold == p.uiFontBold && !fromJson(Json{{"accessibility", 3}}).highContrast);

  report("displayName", displayName("/a/Comic Sans MS.ttf") == "Comic Sans MS" && displayName("OpenDyslexic-Regular.otf") == "OpenDyslexic" && displayName("Lexend-Regular.ttf") == "Lexend" && displayName("Tahoma.ttf") == "Tahoma");
  const std::vector<FontChoice> choices = fontChoices({"/f/Tahoma.ttf", "/f/Tahoma Bold.ttf", "/f/Georgia Italic.ttf", "/f/Georgia.ttf", "/f/Arial Unicode.ttf", "/f/notes.txt", "/u/OpenDyslexic-Regular.otf",
                                                       "/u/OpenDyslexic-Bold.otf", "/f/Trebuchet MS Bold Italic.ttf", "/f/Trebuchet MS.ttf"});
  report("fontChoices: only .ttf/.otf, variants dropped, bold partners paired, recommended first then alphabetical",
         choices.size() == 5 && choices[0].name == "OpenDyslexic" && choices[0].recommended && choices[0].boldPath == "/u/OpenDyslexic-Bold.otf" && choices[1].name == "Tahoma" && choices[1].boldPath == "/f/Tahoma Bold.ttf" &&
             choices[2].name == "Trebuchet MS" && choices[2].boldPath.empty() && choices[3].name == "Arial Unicode" && !choices[3].recommended && choices[4].name == "Georgia" && choices[4].boldPath.empty(),
         choices.empty() ? "none" : choices[0].name + " / " + (choices.size() > 1 ? choices[1].name : "") + " / " + (choices.size() > 2 ? choices[2].name : ""));
  const auto dark = highContrastOverrides(true), light = highContrastOverrides(false);
  report("highContrastOverrides: full slot sets, dark on black with white text, light on white with black text", dark.size() == 15 && light.size() == 15 && dark.at("bg") == "#000000" && dark.at("text") == "#ffffff" &&
                                                                                                                      light.at("bg") == "#ffffff" && light.at("text") == "#000000" && dark.at("border") != light.at("border"));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
