// Usage log: one line per completed reply in support/usage.jsonl (model,
// tokens in/out, time to first token, total time, tok/s, chat, private),
// plus the roll-ups the dashboard draws: per day, per model, per day x
// model, with cost (per-model prices the user enters) and energy (a
// joules-per-token figure for local models). App-free; --usagetest covers it.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace usagelog {

using Json = nlohmann::json;

struct Record {
  int64_t at = 0;              // unix seconds
  std::string model;
  std::string chatId;
  int promptTokens = 0;
  int completionTokens = 0;
  double firstTokenMs = 0.0;   // 0 = unknown
  double totalMs = 0.0;
  double tokensPerSec = 0.0;   // reported by the backend, else derived
  bool isPrivate = false;
  bool isLocal = true;         // local (Ollama) vs paid API
};

void append(const std::string& path, const Record& record);
std::vector<Record> load(const std::string& path, int sinceDays = 0);   // 0 = everything

struct Prices {
  std::map<std::string, std::pair<double, double>> perMillion;   // model -> {$ in, $ out}
  double joulesPerToken = 0.5;                                   // local generation, rough
  double wattHoursIdle = 0.0;
};

struct Row {
  std::string key;             // day "2026-10-24" or model name
  int replies = 0;
  int64_t promptTokens = 0, completionTokens = 0;
  double totalMs = 0.0;
  double tokensPerSecMean = 0.0;   // weighted by completion tokens
  double costUsd = 0.0;
  double energyWh = 0.0;
};

std::vector<Row> byDay(const std::vector<Record>& records, const Prices& prices);
std::vector<Row> byModel(const std::vector<Record>& records, const Prices& prices);
// Day -> model -> completion tokens, for the stacked chart; `models` lists the keys seen.
std::map<std::string, std::map<std::string, int64_t>> dayModelMatrix(const std::vector<Record>& records,
                                                                    std::vector<std::string>* models);
Row totals(const std::vector<Record>& records, const Prices& prices);

std::string csv(const std::vector<Record>& records);
std::string dayOf(int64_t unixSeconds);   // local date, "YYYY-MM-DD"

}  // namespace usagelog
