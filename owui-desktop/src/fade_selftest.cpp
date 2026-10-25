// `Open WebUI --fadetest`: the fade-in timeline.
#include <cmath>
#include <cstdio>
#include <string>

#include "core/fade.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runFadeSelfTest() {
  using fade::Timeline;
  printf("Open WebUI desktop streaming fade-in self-test\n\n");
  Timeline t;
  t.record(0.0, 40);   // 40 characters already there when the reply started being tracked
  report("text present when tracking starts is fully shown", t.alpha(0, 0.0, 0.3) == 1.0f && t.alpha(39, 0.0, 0.3) == 1.0f && !t.fading(0.0, 0.3));
  report("characters not yet recorded are invisible (they arrive this frame)", t.alpha(40, 0.0, 0.3) == 0.0f);
  t.record(1.0, 60);
  t.record(1.1, 90);
  report("arrival: each character gets the time it first appeared", t.arrival(45) == 1.0 && t.arrival(59) == 1.0 && t.arrival(60) == 1.1 && t.arrival(89) == 1.1 && t.arrival(10) == -1.0);
  const float mid = t.alpha(50, 1.15, 0.3), done = t.alpha(50, 1.4, 0.3), fresh = t.alpha(70, 1.1, 0.3);
  report("alpha eases from 0 to 1 over the duration", fresh == 0.0f && mid > 0.2f && mid < 0.8f && done == 1.0f && t.alpha(70, 1.25, 0.3) == 0.5f, std::to_string(mid));
  report("fading while recent text is still ramping up", t.fading(1.2, 0.3) && !t.fading(1.5, 0.3));
  t.record(1.2, 90);
  report("no growth adds no mark", t.arrival(89) == 1.1 && t.total() == 90);
  t.forget(5.0, 1.0);
  report("forget: old marks fold into the settled prefix", t.alpha(85, 5.0, 0.3) == 1.0f && t.arrival(85) == -1.0);
  t.record(5.1, 30);
  report("shrinking text (a final replacement) restarts the timeline with everything shown", t.alpha(20, 5.1, 0.3) == 1.0f && t.total() == 30);
  Timeline z;
  report("a zero duration never hides text", [&] {
    z.record(0, 0);
    z.record(1, 10);
    return z.alpha(5, 1.0, 0.0) == 1.0f;
  }());
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
