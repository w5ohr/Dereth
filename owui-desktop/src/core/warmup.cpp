#include "warmup.h"

#include <algorithm>

namespace warmup {

Plan fromJson(const Json& extra) {
  Plan p;
  const Json j = extra.is_object() ? extra.value("warmup", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", p.enabled);
  p.keepAliveMinutes = std::clamp(j.value("keepAliveMinutes", p.keepAliveMinutes), 1, 24 * 60);
  p.onlyWhileFrontmost = j.value("onlyWhileFrontmost", p.onlyWhileFrontmost);
  p.releaseOnSleep = j.value("releaseOnSleep", p.releaseOnSleep);
  p.releaseOnQuit = j.value("releaseOnQuit", p.releaseOnQuit);
  return p;
}

Json toJson(const Plan& p) {
  return Json{{"enabled", p.enabled},
              {"keepAliveMinutes", p.keepAliveMinutes},
              {"onlyWhileFrontmost", p.onlyWhileFrontmost},
              {"releaseOnSleep", p.releaseOnSleep},
              {"releaseOnQuit", p.releaseOnQuit}};
}

std::string keepAliveText(int minutes) { return minutes <= 0 ? "0" : std::to_string(minutes) + "m"; }

Json loadPayload(const std::string& model, int keepAliveMinutes) {
  return Json{{"model", model}, {"prompt", ""}, {"stream", false}, {"keep_alive", keepAliveText(keepAliveMinutes)}};
}

Json releasePayload(const std::string& model) { return Json{{"model", model}, {"prompt", ""}, {"stream", false}, {"keep_alive", "0"}}; }

double renewIntervalMs(const Plan& plan) { return std::max(60.0, plan.keepAliveMinutes * 60.0 / 2.0) * 1000.0; }

bool shouldRenew(double lastRenewMs, double nowMs, const Plan& plan, bool frontmost) {
  if (!plan.enabled) return false;
  if (plan.onlyWhileFrontmost && !frontmost) return false;
  return nowMs - lastRenewMs >= renewIntervalMs(plan);
}

}  // namespace warmup
