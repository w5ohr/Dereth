#include "lowpower.h"

#include <algorithm>

namespace lowpower {

Policy fromJson(const Json& extra) {
  Policy p;
  const Json j = extra.is_object() ? extra.value("lowPower", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", p.enabled);
  p.idleAfterSec = std::clamp(j.value("idleAfterSec", p.idleAfterSec), 1, 600);
  p.idleFps = std::clamp(j.value("idleFps", p.idleFps), 1, 60);
  p.batteryFps = std::clamp(j.value("batteryFps", p.batteryFps), 1, 60);
  p.deferOnBattery = j.value("deferOnBattery", p.deferOnBattery);
  return p;
}

Json toJson(const Policy& p) {
  return Json{{"enabled", p.enabled}, {"idleAfterSec", p.idleAfterSec}, {"idleFps", p.idleFps}, {"batteryFps", p.batteryFps}, {"deferOnBattery", p.deferOnBattery}};
}

int targetFps(const Policy& p, double idleSeconds, bool onBattery, bool busy) {
  if (!p.enabled) return p.activeFps;
  int fps = p.activeFps;
  if (onBattery) fps = std::min(fps, p.batteryFps);
  if (!busy && idleSeconds >= p.idleAfterSec) fps = std::min(fps, p.idleFps);
  return std::max(1, fps);
}

bool deferHeavyWork(const Policy& p, bool onBattery) { return p.enabled && p.deferOnBattery && onBattery; }

const char* stateName(const Policy& p, double idleSeconds, bool onBattery, bool busy) {
  if (!p.enabled) return "off";
  if (busy) return onBattery ? "busy, on battery" : "busy";
  if (idleSeconds >= p.idleAfterSec) return onBattery ? "idle, on battery" : "idle";
  return onBattery ? "active, on battery" : "active";
}

}  // namespace lowpower
