// `Open WebUI --lowpowertest`: offline checks of the low-power policy.
#include <cstdio>
#include <string>

#include "core/lowpower.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runLowPowerSelfTest() {
  using namespace lowpower;
  printf("Open WebUI desktop low-power self-test\n\n");

  Policy d = fromJson(Json::object());
  report("defaults: on, idle after 8 s at 8 fps, 30 fps on battery, defer on battery", d.enabled && d.idleAfterSec == 8 && d.idleFps == 8 && d.batteryFps == 30 && d.deferOnBattery);
  Policy p = fromJson(Json{{"lowPower", Json{{"idleFps", 0}, {"idleAfterSec", 5000}, {"batteryFps", 20}}}});
  report("fromJson clamps and round-trips", p.idleFps == 1 && p.idleAfterSec == 600 && p.batteryFps == 20 && toJson(p)["batteryFps"] == 20 && fromJson(Json{{"lowPower", toJson(p)}}).batteryFps == 20);

  report("targetFps: active on mains is full rate", targetFps(d, 0, false, false) == 60 && targetFps(d, 7.9, false, false) == 60);
  report("targetFps: idle drops to the idle rate", targetFps(d, 8, false, false) == 8 && targetFps(d, 600, false, false) == 8);
  report("targetFps: busy never idles", targetFps(d, 600, false, true) == 60 && targetFps(d, 600, true, true) == 30);
  report("targetFps: battery caps the active rate, idle still lower", targetFps(d, 0, true, false) == 30 && targetFps(d, 20, true, false) == 8);
  Policy off = d;
  off.enabled = false;
  report("targetFps: off means full rate always", targetFps(off, 600, true, false) == 60);
  report("deferHeavyWork only on battery with the switch on", deferHeavyWork(d, true) && !deferHeavyWork(d, false) && !deferHeavyWork(off, true) &&
                                                             [&] { Policy n = d; n.deferOnBattery = false; return !deferHeavyWork(n, true); }());
  report("stateName", std::string(stateName(d, 0, false, false)) == "active" && std::string(stateName(d, 10, true, false)) == "idle, on battery" &&
                          std::string(stateName(d, 10, false, true)) == "busy" && std::string(stateName(off, 10, false, false)) == "off");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
