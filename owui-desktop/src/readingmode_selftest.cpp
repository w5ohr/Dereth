// `Open WebUI --readingtest`: offline checks of reading mode.
#include <cstdio>
#include <string>

#include "core/readingmode.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runReadingModeSelfTest() {
  using namespace readingmode;
  printf("Open WebUI desktop reading-mode self-test\n\n");

  const Prefs d = fromJson(Json::object());
  report("defaults: off, 680 px, 115%, 1.3x, sidebar hidden, controls on hover", !d.enabled && d.measure == 680.0f && d.textScale == 1.15f && d.lineSpacing == 1.3f && d.hideSidebar && d.controlsOnHover);
  const Prefs p = fromJson(Json{{"readingMode", Json{{"enabled", true}, {"measure", 5000}, {"textScale", 0.2}, {"lineSpacing", 1.5}, {"controlsOnHover", false}}}});
  report("fromJson clamps and round-trips", p.enabled && p.measure == 1000.0f && p.textScale == 1.0f && p.lineSpacing == 1.5f && !p.controlsOnHover && fromJson(Json{{"readingMode", toJson(p)}}).lineSpacing == 1.5f &&
                                                 !fromJson(Json{{"readingMode", "x"}}).enabled);
  Prefs on = d;
  on.enabled = true;
  report("contentWidth: the measure when on, the default when off, never wider than the room", contentWidth(on, 1200, 820) == 680.0f && contentWidth(d, 1200, 820) == 820.0f && contentWidth(on, 500, 820) == 460.0f &&
                                                                                                 contentWidth(d, 100, 820) == 200.0f);
  report("itemSpacingY", itemSpacingY(on, 7.0f) > 9.0f && itemSpacingY(on, 7.0f) < 9.2f && itemSpacingY(d, 7.0f) == 7.0f);
  report("showControls: always when off or not on-hover; else hovered or focused", showControls(d, false, false) && showControls(p, false, false) && !showControls(on, false, false) && showControls(on, true, false) &&
                                                                                      showControls(on, false, true));
  report("summary", summary(on) == "680 px measure, text 115%, line spacing 1.3x");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
