// `Open WebUI --virtualtest`: offline checks of the virtualised transcript.
#include <cstdio>
#include <string>

#include "core/virtualize.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runVirtualizeSelfTest() {
  using namespace virtualize;
  printf("Open WebUI desktop virtualised-transcript self-test\n\n");

  Heights h;
  report("unknown heights read as -1", h.known("a", 800.0f) < 0.0f && h.size() == 0);
  h.remember("a", 120.0f, 800.0f, 1);
  h.remember("b", 40.0f, 800.0f, 1);
  report("remember / known at the same width; another width forgets the measure", h.known("a", 800.0f) == 120.0f && h.known("a", 800.4f) == 120.0f && h.known("a", 680.0f) < 0.0f && h.size() == 2);
  h.remember("a", 150.0f, 680.0f, 2);
  report("re-measuring replaces the entry", h.known("a", 680.0f) == 150.0f && h.known("a", 800.0f) < 0.0f && h.size() == 2);
  h.prune(100, 50);
  report("prune drops stale entries, forget drops one, clear empties", h.size() == 0 && [&] { h.remember("c", 1, 1, 5); h.forget("c"); return h.size() == 0; }() && [&] { h.remember("d", 1, 1, 5); h.clear(); return h.size() == 0; }());

  report("shouldSkip: wholly above or below the view plus margin", shouldSkip(-500, 100, 0, 800, 200) && shouldSkip(1100, 100, 0, 800, 200) && !shouldSkip(-250, 100, 0, 800, 200) && !shouldSkip(900, 100, 0, 800, 200) &&
                                                                    !shouldSkip(300, 100, 0, 800, 200) && !shouldSkip(-500, 0, 0, 800, 200));
  report("statsLine", statsLine(Stats{12, 1488, 1.84}) == "drew 12 of 1500 messages in 1.8 ms");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
