// Token budgets (#79): per-chat and per-day token caps and a per-day cost
// cap over the usage log, with a warning threshold. Pure: the app loads the
// records, asks for the spend and the verdict, and decides what to show.
// App-free; --budgettest covers it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "usagelog.h"

namespace budgets {

using Json = nlohmann::json;

struct Limits {
  int64_t chatTokens = 0;   // 0 = no cap
  int64_t dayTokens = 0;
  double dayUsd = 0.0;
  double warnAt = 0.8;      // fraction of a cap that raises a warning
  bool any() const { return chatTokens > 0 || dayTokens > 0 || dayUsd > 0.0; }
};
Limits fromJson(const Json& j);   // settings extra["budgets"]
Json toJson(const Limits& limits);

struct Spend {
  int64_t chatTokens = 0;   // prompt + completion, this chat, all time
  int64_t dayTokens = 0;    // prompt + completion, today (local date)
  double dayUsd = 0.0;      // today, models with a price
  double chatUsd = 0.0;
};
Spend spend(const std::vector<usagelog::Record>& records, const std::string& chatId, const usagelog::Prices& prices,
            int64_t nowSeconds);

enum class Level { Ok, Warn, Over };
struct Verdict {
  Level level = Level::Ok;
  std::string message;      // what crossed, with the numbers
};
// `aboutToAdd` is the estimated tokens of the send being considered.
Verdict check(const Limits& limits, const Spend& spend, int64_t aboutToAdd);

// "today 12.3k / 50k tokens, $0.42 / $2.00; this chat 3.1k / 20k" (only the caps set).
std::string summary(const Limits& limits, const Spend& spend);
std::string tokens(int64_t n);   // 12.3k

}  // namespace budgets
