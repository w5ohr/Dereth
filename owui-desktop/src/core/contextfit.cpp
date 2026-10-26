#include "contextfit.h"

#include <algorithm>

namespace contextfit {

const char* strategyKey(Strategy s) {
  switch (s) {
    case Strategy::DropOldest: return "drop";
    case Strategy::Window: return "window";
    case Strategy::Summary: return "summary";
  }
  return "drop";
}

Strategy strategyFromKey(const std::string& key) {
  if (key == "window") return Strategy::Window;
  if (key == "summary") return Strategy::Summary;
  return Strategy::DropOldest;
}

const char* strategyLabel(Strategy s) {
  switch (s) {
    case Strategy::DropOldest: return "Drop the oldest turns";
    case Strategy::Window: return "Sliding window of recent turns";
    case Strategy::Summary: return "Rolling summary of the oldest turns";
  }
  return "";
}

Plan fit(const std::vector<Msg>& messages, int budget, Strategy strategy, int windowTurns, int summaryCovers,
         int summaryTokens) {
  Plan plan;
  plan.budget = budget;
  const int n = static_cast<int>(messages.size());
  std::vector<bool> keep(static_cast<size_t>(n), true);
  for (const Msg& m : messages) plan.tokensBefore += m.tokens;

  // The newest user turn (and anything after it) is never cut.
  int lastUser = -1;
  for (int i = n - 1; i >= 0; --i) {
    if (messages[static_cast<size_t>(i)].role == "user") {
      lastUser = i;
      break;
    }
  }
  auto cuttable = [&](int i) {
    const Msg& m = messages[static_cast<size_t>(i)];
    return m.role != "system" && (lastUser < 0 || i < lastUser);
  };
  auto total = [&] {
    int t = 0;
    for (int i = 0; i < n; ++i) {
      if (keep[static_cast<size_t>(i)]) t += messages[static_cast<size_t>(i)].tokens;
    }
    return t;
  };

  // Rolling summary: the covered prefix is replaced by the summary when that
  // helps (it always does once the prefix is bigger than the summary).
  int summaryCost = 0;
  if (strategy == Strategy::Summary && summaryCovers > 0 && summaryCovers <= n && total() > budget) {
    int prefixTokens = 0;
    for (int i = 0; i < summaryCovers; ++i) {
      if (cuttable(i)) prefixTokens += messages[static_cast<size_t>(i)].tokens;
    }
    if (prefixTokens > summaryTokens) {
      for (int i = 0; i < summaryCovers; ++i) {
        if (cuttable(i)) keep[static_cast<size_t>(i)] = false;
      }
      plan.summaryUsed = true;
      summaryCost = summaryTokens;
    }
  }

  // Sliding window: at most `windowTurns` user turns (with their replies).
  if (strategy == Strategy::Window && windowTurns > 0) {
    // A turn is a user message and the replies after it; walking back, the
    // window is full once `windowTurns` user messages have been passed.
    int userTurns = 0;
    for (int i = n - 1; i >= 0; --i) {
      const Msg& m = messages[static_cast<size_t>(i)];
      if (userTurns >= windowTurns && cuttable(i)) keep[static_cast<size_t>(i)] = false;
      if (m.role == "user") ++userTurns;
    }
  }

  // Then the oldest cuttable messages go until it fits.
  for (int i = 0; i < n && total() + summaryCost > budget; ++i) {
    if (keep[static_cast<size_t>(i)] && cuttable(i)) keep[static_cast<size_t>(i)] = false;
  }

  for (int i = 0; i < n; ++i) (keep[static_cast<size_t>(i)] ? plan.keep : plan.cut).push_back(i);
  plan.tokensAfter = total() + summaryCost;
  return plan;
}

}  // namespace contextfit
