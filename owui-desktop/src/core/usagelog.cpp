#include "usagelog.h"

#include <algorithm>
#include <ctime>
#include <fstream>

#include "common.h"

namespace usagelog {
namespace {

Json toJson(const Record& r) {
  return Json{{"at", r.at},
              {"model", r.model},
              {"chat", r.chatId},
              {"in", r.promptTokens},
              {"out", r.completionTokens},
              {"ttft", r.firstTokenMs},
              {"ms", r.totalMs},
              {"tps", r.tokensPerSec},
              {"private", r.isPrivate},
              {"local", r.isLocal}};
}

Record fromJson(const Json& j) {
  Record r;
  r.at = j.value("at", static_cast<int64_t>(0));
  r.model = j.value("model", "");
  r.chatId = j.value("chat", "");
  r.promptTokens = j.value("in", 0);
  r.completionTokens = j.value("out", 0);
  r.firstTokenMs = j.value("ttft", 0.0);
  r.totalMs = j.value("ms", 0.0);
  r.tokensPerSec = j.value("tps", 0.0);
  r.isPrivate = j.value("private", false);
  r.isLocal = j.value("local", true);
  return r;
}

void accumulate(Row* row, const Record& r, const Prices& prices) {
  row->replies += 1;
  row->promptTokens += r.promptTokens;
  row->completionTokens += r.completionTokens;
  row->totalMs += r.totalMs;
  auto price = prices.perMillion.find(r.model);
  if (price != prices.perMillion.end()) {
    row->costUsd += r.promptTokens * price->second.first / 1e6 + r.completionTokens * price->second.second / 1e6;
  }
  if (r.isLocal) {
    row->energyWh += (r.promptTokens + r.completionTokens) * prices.joulesPerToken / 3600.0;
  }
}

void finishRate(Row* row, double weightedTps, int64_t weight) {
  row->tokensPerSecMean = weight > 0 ? weightedTps / static_cast<double>(weight) : 0.0;
}

std::vector<Row> group(const std::vector<Record>& records, const Prices& prices,
                       const std::function<std::string(const Record&)>& keyOf, bool sortDesc) {
  std::map<std::string, Row> rows;
  std::map<std::string, double> weighted;
  std::map<std::string, int64_t> weights;
  for (const Record& r : records) {
    const std::string key = keyOf(r);
    Row& row = rows[key];
    row.key = key;
    accumulate(&row, r, prices);
    if (r.tokensPerSec > 0 && r.completionTokens > 0) {
      weighted[key] += r.tokensPerSec * r.completionTokens;
      weights[key] += r.completionTokens;
    }
  }
  std::vector<Row> out;
  for (auto& [key, row] : rows) {
    finishRate(&row, weighted[key], weights[key]);
    out.push_back(row);
  }
  if (sortDesc) std::reverse(out.begin(), out.end());
  return out;
}

}  // namespace

std::string dayOf(int64_t unixSeconds) {
  std::time_t t = static_cast<std::time_t>(unixSeconds);
  std::tm local{};
  localtime_r(&t, &local);
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
  return buf;
}

void append(const std::string& path, const Record& record) {
  std::ofstream f(path, std::ios::app);
  if (f) f << toJson(record).dump() << "\n";
}

std::vector<Record> load(const std::string& path, int sinceDays) {
  std::vector<Record> out;
  bool ok = false;
  const std::string raw = core::readFile(path, &ok);
  if (!ok) return out;
  const int64_t cutoff = sinceDays > 0 ? static_cast<int64_t>(std::time(nullptr)) - int64_t(sinceDays) * 86400 : 0;
  for (const std::string& line : core::split(raw, '\n')) {
    if (core::trim(line).empty()) continue;
    Json j = Json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) continue;
    Record r = fromJson(j);
    if (r.at >= cutoff) out.push_back(r);
  }
  return out;
}

std::vector<Row> byDay(const std::vector<Record>& records, const Prices& prices) {
  return group(records, prices, [](const Record& r) { return dayOf(r.at); }, true);
}

std::vector<Row> byModel(const std::vector<Record>& records, const Prices& prices) {
  std::vector<Row> rows = group(records, prices, [](const Record& r) { return r.model; }, false);
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.promptTokens + a.completionTokens > b.promptTokens + b.completionTokens;
  });
  return rows;
}

std::map<std::string, std::map<std::string, int64_t>> dayModelMatrix(const std::vector<Record>& records,
                                                                    std::vector<std::string>* models) {
  std::map<std::string, std::map<std::string, int64_t>> out;
  std::map<std::string, int64_t> perModel;
  for (const Record& r : records) {
    out[dayOf(r.at)][r.model] += r.completionTokens;
    perModel[r.model] += r.completionTokens;
  }
  if (models) {
    models->clear();
    for (const auto& [m, n] : perModel) models->push_back(m);
    std::sort(models->begin(), models->end(), [&](const std::string& a, const std::string& b) { return perModel[a] > perModel[b]; });
  }
  return out;
}

Row totals(const std::vector<Record>& records, const Prices& prices) {
  Row row;
  row.key = "total";
  double weighted = 0.0;
  int64_t weight = 0;
  for (const Record& r : records) {
    accumulate(&row, r, prices);
    if (r.tokensPerSec > 0 && r.completionTokens > 0) {
      weighted += r.tokensPerSec * r.completionTokens;
      weight += r.completionTokens;
    }
  }
  finishRate(&row, weighted, weight);
  return row;
}

std::string csv(const std::vector<Record>& records) {
  std::string out = "time,day,model,chat,prompt_tokens,completion_tokens,first_token_ms,total_ms,tokens_per_s,private,local\n";
  for (const Record& r : records) {
    std::time_t t = static_cast<std::time_t>(r.at);
    std::tm local{};
    localtime_r(&t, &local);
    char when[32];
    std::strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%S", &local);
    char line[512];
    snprintf(line, sizeof(line), "%s,%s,\"%s\",%s,%d,%d,%.0f,%.0f,%.1f,%d,%d\n", when, dayOf(r.at).c_str(),
             r.model.c_str(), r.isPrivate ? "" : r.chatId.c_str(), r.promptTokens, r.completionTokens,
             r.firstTokenMs, r.totalMs, r.tokensPerSec, r.isPrivate ? 1 : 0, r.isLocal ? 1 : 0);
    out += line;
  }
  return out;
}

}  // namespace usagelog
