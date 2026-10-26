#include "readingmode.h"

#include <algorithm>
#include <cmath>

namespace readingmode {

Prefs fromJson(const Json& extra) {
  Prefs p;
  const Json j = extra.is_object() ? extra.value("readingMode", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", p.enabled);
  p.measure = std::clamp(j.value("measure", p.measure), 480.0f, 1000.0f);
  p.textScale = std::clamp(j.value("textScale", p.textScale), 1.0f, 1.6f);
  p.lineSpacing = std::clamp(j.value("lineSpacing", p.lineSpacing), 1.0f, 2.0f);
  p.hideSidebar = j.value("hideSidebar", p.hideSidebar);
  p.controlsOnHover = j.value("controlsOnHover", p.controlsOnHover);
  return p;
}

Json toJson(const Prefs& p) {
  return Json{{"enabled", p.enabled}, {"measure", p.measure}, {"textScale", p.textScale}, {"lineSpacing", p.lineSpacing}, {"hideSidebar", p.hideSidebar}, {"controlsOnHover", p.controlsOnHover}};
}

float contentWidth(const Prefs& p, float available, float defaultMax) {
  const float room = std::max(200.0f, available - 40.0f);
  return std::min(p.enabled ? p.measure : defaultMax, room);
}

float itemSpacingY(const Prefs& p, float base) { return p.enabled ? base * p.lineSpacing : base; }

bool showControls(const Prefs& p, bool hovered, bool focused) {
  if (!p.enabled || !p.controlsOnHover) return true;
  return hovered || focused;
}

std::string summary(const Prefs& p) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%d px measure, text %d%%, line spacing %.1fx", static_cast<int>(std::lround(p.measure)), static_cast<int>(std::lround(p.textScale * 100.0f)), p.lineSpacing);
  return buf;
}

}  // namespace readingmode
