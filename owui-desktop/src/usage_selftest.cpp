// `Open WebUI --usagetest`: offline checks of the usage log and its roll-ups.
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "core/usagelog.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runUsageSelfTest() {
  using namespace usagelog;
  printf("Open WebUI desktop usage-log self-test\n\n");
  const std::string path = "/tmp/owui-usagetest-" + std::to_string(getpid()) + ".jsonl";
  const int64_t now = static_cast<int64_t>(std::time(nullptr)) / 86400 * 86400 + 43200;   // today, midday UTC
  const int64_t day = 86400;
  Record a{now - 2 * day, "qwen3:8b", "c1", 1000, 500, 400.0, 10400.0, 50.0, false, true};
  Record b{now - 2 * day + 3600, "llama3.3:70b", "c2", 2000, 1000, 900.0, 100900.0, 10.0, false, true};
  Record c{now, "gpt-4o", "c3", 3000, 1500, 300.0, 30300.0, 50.0, false, false};
  Record d{now + 60, "qwen3:8b", "p1", 100, 100, 100.0, 2100.0, 0.0, true, true};
  for (const Record& r : {a, b, c, d}) append(path, r);

  auto all = load(path, 0);
  report("append + load round-trip", all.size() == 4 && all[1].model == "llama3.3:70b" && all[3].isPrivate && !all[2].isLocal);
  Prices prices;
  prices.perMillion["gpt-4o"] = {2.5, 10.0};
  prices.joulesPerToken = 0.5;
  Row t = totals(all, prices);
  report("totals tokens", t.promptTokens == 6100 && t.completionTokens == 3100 && t.replies == 4);
  report("cost only for priced models", std::abs(t.costUsd - (3000 * 2.5 / 1e6 + 1500 * 10.0 / 1e6)) < 1e-9, std::to_string(t.costUsd));
  report("energy only for local tokens", std::abs(t.energyWh - (1500 + 3000 + 200) * 0.5 / 3600.0) < 1e-9, std::to_string(t.energyWh));
  // Weighted tok/s: (50*500 + 10*1000 + 50*1500) / 3000 = 36.67; d has no rate.
  report("weighted tok/s", std::abs(t.tokensPerSecMean - 36.6667) < 0.01, std::to_string(t.tokensPerSecMean));

  auto days = byDay(all, prices);
  report("two days, newest first", days.size() == 2 && days[0].key > days[1].key && days[0].replies == 2 && days[1].replies == 2);
  auto models = byModel(all, prices);
  report("models by volume", models.size() == 3 && models[0].key == "gpt-4o" && models[2].key == "qwen3:8b", models.empty() ? "" : models[0].key);
  std::vector<std::string> seen;
  auto matrix = dayModelMatrix(all, &seen);
  report("matrix", matrix.size() == 2 && seen.size() == 3 && seen[0] == "gpt-4o" && matrix[dayOf(now)]["qwen3:8b"] == 100);
  const std::string text = csv(all);
  report("csv header + rows", text.rfind("time,day,model,chat,", 0) == 0 && std::count(text.begin(), text.end(), '\n') == 5);
  report("csv hides private chat ids", text.find("p1") == std::string::npos && text.find(",c1,") != std::string::npos);
  report("sinceDays filters", load(path, 1).size() == 2, std::to_string(load(path, 1).size()));
  unlink(path.c_str());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
