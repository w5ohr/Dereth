// Context fitting: when a conversation outgrows the model's window, decide
// which messages still go to the model. Three strategies - drop the oldest,
// a sliding window of recent turns, or a rolling summary standing in for the
// oldest part - all keep every system message and the newest user turn.
// App-free; --contexttest drives it offline.
#pragma once

#include <string>
#include <vector>

namespace contextfit {

enum class Strategy { DropOldest, Window, Summary };

const char* strategyKey(Strategy s);          // "drop" | "window" | "summary"
Strategy strategyFromKey(const std::string& key);
const char* strategyLabel(Strategy s);

struct Msg {
  std::string role;     // system | user | assistant | tool
  int tokens = 0;
  std::string id;       // optional, for the caller's bookkeeping
};

struct Plan {
  std::vector<int> keep;        // indices into the input, in order
  std::vector<int> cut;         // indices left out (dropped or folded into the summary)
  bool summaryUsed = false;     // a summary message stands in for `cut`
  int tokensBefore = 0;
  int tokensAfter = 0;          // including the summary when used
  int budget = 0;
};

// `summaryCovers` is how many leading messages an existing summary stands for
// (0 = none); `summaryTokens` its size. `windowTurns` is the number of
// user+assistant pairs a sliding window keeps.
Plan fit(const std::vector<Msg>& messages, int budget, Strategy strategy, int windowTurns, int summaryCovers,
         int summaryTokens);

}  // namespace contextfit
