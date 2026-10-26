// `Open WebUI --pooltest`: offline checks of the multi-server pool.
#include <cstdio>
#include <string>

#include "core/pool.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
pool::Health h(const std::string& id, bool alive, int running, int inFlight, double ms) {
  pool::Health x;
  x.id = id;
  x.alive = alive;
  x.running = running;
  x.inFlight = inFlight;
  x.latencyMs = ms;
  x.checkedAt = 100;
  return x;
}
}  // namespace

int runPoolSelfTest() {
  using namespace pool;
  printf("Open WebUI desktop backend-pool self-test\n\n");

  const Prefs d = fromJson(Json::object());
  report("defaults: off, no members, least-loaded, 30 s, failover", !d.enabled && d.members.empty() && d.strategy == "least-loaded" && d.checkEverySec == 30 && d.failover);
  const Prefs p = fromJson(Json{{"backendPool", Json{{"enabled", true}, {"members", Json::array({"a", "b", "a", 3})}, {"strategy", "bogus"}, {"checkEverySec", 1}, {"failover", false}}}});
  report("fromJson: members deduped and strings only, strategy validated, interval clamped, round-trip", p.enabled && p.members.size() == 2 && p.strategy == "least-loaded" && p.checkEverySec == 5 && !p.failover &&
                                                                                                           fromJson(Json{{"backendPool", toJson(p)}}).members == p.members);
  report("runningFrom: Ollama /api/ps counts loaded models; other shapes give 0", runningFrom(Json{{"models", Json::array({Json{{"name", "x"}, {"size_vram", 1}}, Json{{"name", "y"}, {"expires_at", "t"}}})}}) == 2 &&
                                                                                      runningFrom(Json{{"data", Json::array()}}) == 0 && runningFrom(Json()) == 0);

  Prefs prefs;
  prefs.enabled = true;
  prefs.members = {"a", "b", "c"};
  const std::vector<Health> health{h("a", true, 2, 0, 20), h("b", true, 0, 0, 500), h("c", false, 0, 0, 0)};
  report("least-loaded: fewest in-flight, then loaded models, then latency; dead members skipped", pick(prefs, health, 0) == "b" && order(prefs, health, 0) == std::vector<std::string>({"b", "a"}) &&
                                                                                                      [&] { auto hh = health; hh[1].inFlight = 1; return pick(prefs, hh, 0) == "a"; }());
  prefs.strategy = "round-robin";
  report("round-robin rotates through the alive members", pick(prefs, health, 0) == "a" && pick(prefs, health, 1) == "b" && pick(prefs, health, 2) == "a" && order(prefs, health, 1) == std::vector<std::string>({"b", "a"}));
  prefs.strategy = "first-alive";
  report("first-alive keeps the configured order; unknown health counts as alive; none alive is empty", pick(prefs, health, 5) == "a" && pick(prefs, {}, 0) == "a" &&
                                                                                                           [&] { std::vector<Health> dead{h("a", false, 0, 0, 0), h("b", false, 0, 0, 0), h("c", false, 0, 0, 0)}; return pick(prefs, dead, 0).empty() && order(prefs, dead, 0).empty(); }());
  report("describe", describe(health[0]) == "alive - 2 models loaded, 0 in flight, 20 ms" && describe(health[2]) == "down: no answer" && describe(Health{}) == "not checked yet" &&
                         [&] { Health e = health[2]; e.error = "connection refused"; return describe(e) == "down: connection refused"; }());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
