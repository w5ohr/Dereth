// `Open WebUI --budgettest`: offline checks of the budget arithmetic.
#include <ctime>
#include <cstdio>
#include <string>

#include "core/budgets.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runBudgetSelfTest() {
  using namespace budgets;
  printf("Open WebUI desktop budgets self-test\n\n");

  Limits l = fromJson(Json{{"chatTokens", 20000}, {"dayTokens", 50000}, {"dayUsd", 2.0}, {"warnAt", 0.8}});
  report("fromJson / toJson / any", l.any() && l.chatTokens == 20000 && l.dayUsd == 2.0 && toJson(l)["dayTokens"] == 50000 && !fromJson(Json()).any() &&
                                        fromJson(Json{{"warnAt", 5}}).warnAt == 1.0);

  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  usagelog::Prices prices;
  prices.perMillion["gpt-4o"] = {2.5, 10.0};
  std::vector<usagelog::Record> records;
  usagelog::Record a;
  a.at = now - 60;
  a.model = "gpt-4o";
  a.chatId = "c1";
  a.promptTokens = 100000;
  a.completionTokens = 10000;   // $0.25 + $0.10
  usagelog::Record b = a;
  b.model = "qwen3:8b";
  b.chatId = "c2";
  b.promptTokens = 5000;
  b.completionTokens = 1000;
  usagelog::Record old = a;
  old.at = now - 3 * 86400;   // yesterday or earlier: not today
  old.chatId = "c1";
  records = {a, b, old};
  Spend s = spend(records, "c1", prices, now);
  report("spend: today's tokens and cost, chat totals across days", s.dayTokens == 116000 && s.chatTokens == 220000 && s.dayUsd > 0.349 && s.dayUsd < 0.351 &&
                                                                        s.chatUsd > 0.699 && s.chatUsd < 0.701,
         tokens(s.dayTokens) + " / $" + std::to_string(s.dayUsd));

  Verdict ok = check(Limits{}, s, 500);
  report("no caps: ok", ok.level == Level::Ok && ok.message.empty());
  Limits warn{300000, 150000, 5.0, 0.75};
  Verdict w = check(warn, s, 0);
  report("warn: day tokens at 77% of 150k", w.level == Level::Warn && w.message.find("today's tokens") != std::string::npos && w.message.find("77%") != std::string::npos, w.message);
  Limits over{200000, 0, 0.30, 0.8};
  Verdict o = check(over, s, 1000);
  report("over: chat tokens past the cap and cost past $0.30, both named", o.level == Level::Over && o.message.find("this chat's tokens") != std::string::npos &&
                                                                               o.message.find("today's cost") != std::string::npos && o.message.find("$0.35 of $0.30") != std::string::npos, o.message);
  Verdict about = check(Limits{120000, 0, 0, 0.8}, s, 0);
  Verdict aboutAdd = check(Limits{221000, 0, 0, 0.8}, s, 2000);
  report("aboutToAdd pushes a chat over", about.level == Level::Over && aboutAdd.level == Level::Over);
  report("summary names only the caps set", summary(Limits{20000, 0, 0, 0.8}, s) == "this chat 220.0k / 20.0k" &&
                                                 summary(Limits{0, 50000, 2.0, 0.8}, s) == "today 116.0k / 50.0k tokens, $0.35 / $2.00" && summary(Limits{}, s).empty(),
         summary(Limits{0, 50000, 2.0, 0.8}, s));
  report("tokens formatting", tokens(999) == "999" && tokens(12345) == "12.3k" && tokens(2500000) == "2.50M");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
