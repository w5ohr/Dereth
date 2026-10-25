// `Open WebUI --contexttest`: offline checks of the context-fitting planner.
#include <cstdio>
#include <string>
#include <vector>

#include "core/contextfit.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string show(const contextfit::Plan& p) {
  std::string s = "keep[";
  for (int i : p.keep) s += std::to_string(i) + " ";
  s += "] cut[";
  for (int i : p.cut) s += std::to_string(i) + " ";
  return s + "] after=" + std::to_string(p.tokensAfter) + (p.summaryUsed ? " +summary" : "");
}
}  // namespace

int runContextSelfTest() {
  using namespace contextfit;
  printf("Open WebUI desktop context-fit self-test\n\n");
  // 0 system(50) 1 user(100) 2 asst(300) 3 user(100) 4 asst(300) 5 user(100) 6 asst(300) 7 user(100)
  std::vector<Msg> msgs = {{"system", 50, "s"}, {"user", 100, "u1"}, {"assistant", 300, "a1"}, {"user", 100, "u2"},
                           {"assistant", 300, "a2"}, {"user", 100, "u3"}, {"assistant", 300, "a3"}, {"user", 100, "u4"}};
  // total 1350

  Plan p = fit(msgs, 2000, Strategy::DropOldest, 6, 0, 0);
  report("fits: nothing cut", p.cut.empty() && p.tokensAfter == 1350, show(p));

  p = fit(msgs, 900, Strategy::DropOldest, 6, 0, 0);
  report("drop oldest: oldest non-system go first", p.cut == std::vector<int>{1, 2, 3} && p.keep[0] == 0 && p.tokensAfter == 850, show(p));
  report("drop oldest: newest user kept", std::find(p.keep.begin(), p.keep.end(), 7) != p.keep.end());

  p = fit(msgs, 200, Strategy::DropOldest, 6, 0, 0);
  report("impossible budget: still keeps system + newest user", p.keep == std::vector<int>{0, 7} && p.tokensAfter == 150, show(p));

  p = fit(msgs, 2000, Strategy::Window, 2, 0, 0);
  report("window: last 2 user turns with replies, even when it fits", p.keep == std::vector<int>{0, 5, 6, 7}, show(p));
  p = fit(msgs, 450, Strategy::Window, 2, 0, 0);
  report("window then drop to fit", p.keep == std::vector<int>{0, 7} || p.tokensAfter <= 450, show(p));

  p = fit(msgs, 900, Strategy::Summary, 6, 0, 0);
  report("summary without one on file behaves like drop", !p.summaryUsed && p.cut == std::vector<int>{1, 2, 3}, show(p));
  p = fit(msgs, 900, Strategy::Summary, 6, 5, 120);
  report("summary covering 5 messages replaces them", p.summaryUsed && p.cut == std::vector<int>{1, 2, 3, 4} &&
                                                          p.tokensAfter == 50 + 100 + 300 + 100 + 120, show(p));
  p = fit(msgs, 2000, Strategy::Summary, 6, 5, 120);
  report("summary unused when everything fits", !p.summaryUsed && p.cut.empty(), show(p));
  p = fit(msgs, 600, Strategy::Summary, 6, 5, 120);
  report("summary then drop more if still over", p.summaryUsed && p.tokensAfter <= 600 && p.keep.back() == 7, show(p));
  std::vector<Msg> bigSummary = msgs;
  p = fit(bigSummary, 900, Strategy::Summary, 6, 3, 900);
  report("a summary larger than its prefix is not used", !p.summaryUsed, show(p));

  std::vector<Msg> onlyUser = {{"user", 5000, "u"}};
  p = fit(onlyUser, 100, Strategy::DropOldest, 6, 0, 0);
  report("single oversized user turn is still sent", p.keep == std::vector<int>{0}, show(p));

  report("keys round-trip", strategyFromKey(strategyKey(Strategy::Window)) == Strategy::Window &&
                                strategyFromKey("nonsense") == Strategy::DropOldest);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
