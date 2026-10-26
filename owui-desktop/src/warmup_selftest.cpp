// `Open WebUI --warmuptest`: offline checks of the warmup plan and schedule.
#include <cstdio>
#include <string>

#include "core/warmup.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runWarmupSelfTest() {
  using namespace warmup;
  printf("Open WebUI desktop warmup self-test\n\n");

  Plan d = fromJson(Json::object());
  report("defaults: off, 30 min, frontmost only, release on sleep, keep on quit", !d.enabled && d.keepAliveMinutes == 30 && d.onlyWhileFrontmost && d.releaseOnSleep && !d.releaseOnQuit);
  Plan p = fromJson(Json{{"warmup", Json{{"enabled", true}, {"keepAliveMinutes", 5000}, {"onlyWhileFrontmost", false}}}});
  report("fromJson clamps keep-alive to a day and round-trips", p.enabled && p.keepAliveMinutes == 1440 && !p.onlyWhileFrontmost && toJson(p)["keepAliveMinutes"] == 1440 &&
                                                                    fromJson(Json{{"warmup", toJson(p)}}).enabled);
  report("keepAliveText", keepAliveText(30) == "30m" && keepAliveText(0) == "0" && keepAliveText(-3) == "0");
  Json load = loadPayload("qwen3:8b", 30);
  Json rel = releasePayload("qwen3:8b");
  report("payloads: empty prompt, no stream, keep_alive", load["model"] == "qwen3:8b" && load["prompt"] == "" && load["stream"] == false && load["keep_alive"] == "30m" &&
                                                            rel["keep_alive"] == "0");
  Plan plan;
  plan.enabled = true;
  plan.keepAliveMinutes = 30;
  report("renew interval is half the keep-alive, at least a minute", renewIntervalMs(plan) == 15.0 * 60.0 * 1000.0 && renewIntervalMs(fromJson(Json{{"warmup", Json{{"keepAliveMinutes", 1}}}})) == 60000.0);
  report("shouldRenew: due only after the interval, only when frontmost by default, never when off",
         !shouldRenew(0, 10 * 60 * 1000.0, plan, true) && shouldRenew(0, 15 * 60 * 1000.0, plan, true) && !shouldRenew(0, 20 * 60 * 1000.0, plan, false) &&
             [&] { Plan bg = plan; bg.onlyWhileFrontmost = false; return shouldRenew(0, 20 * 60 * 1000.0, bg, false); }() &&
             [&] { Plan off = plan; off.enabled = false; return !shouldRenew(0, 1e9, off, true); }());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
