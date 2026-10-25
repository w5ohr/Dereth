// `Open WebUI --logprobtest`: offline checks of the logprob view.
#include <cmath>
#include <cstdio>
#include <string>

#include "core/backends.h"
#include "core/logprobs.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runLogprobSelfTest() {
  using namespace logprobs;
  printf("Open WebUI desktop logprob-view self-test\n\n");

  const Prefs d = prefsFromJson(Json::object());
  const Prefs p = prefsFromJson(Json{{"logprobs", Json{{"enabled", true}, {"top", 40}}}});
  report("prefs: off by default, top clamped, round-trip", !d.enabled && d.top == 3 && p.enabled && p.top == 8 && prefsFromJson(Json{{"logprobs", prefsToJson(p)}}).top == 8);

  const Json entries = Json::parse(R"([{"token":" the","logprob":-0.0024,"bytes":[32],"top_logprobs":[{"token":" the","logprob":-0.0024},{"token":" let","logprob":-6.72}]},
                                       {"token":" moon","logprob":-1.2},{"bogus":1}])");
  const auto tokens = fromEntries(entries);
  report("fromEntries: two tokens, alternatives kept, junk skipped", tokens.size() == 2 && tokens[0].token == " the" && tokens[0].alts.size() == 2 && tokens[0].alts[1].token == " let" && tokens[1].alts.empty());
  const Json choice = Json{{"delta", Json{{"content", "x"}}}, {"logprobs", Json{{"content", entries}}}};
  report("fromOpenAIChoice reads choice.logprobs.content; no logprobs gives none", fromOpenAIChoice(choice).size() == 2 && fromOpenAIChoice(Json{{"delta", Json::object()}}).empty());
  const auto back = fromJson(toJson(tokens));
  report("compact JSON round-trip", back.size() == 2 && back[0].token == " the" && std::fabs(back[0].logprob + 0.0024) < 1e-9 && back[0].alts.size() == 2 && back[0].alts[1].token == " let" &&
                                        toJson(tokens)[0].is_array() && toJson(tokens)[0].size() == 3);
  report("confidence: exp, clamped, non-finite is 0", std::fabs(confidence(0.0) - 1.0) < 1e-9 && std::fabs(confidence(-0.693147) - 0.5) < 1e-4 && confidence(2.0) == 1.0 && confidence(-INFINITY) == 0.0);
  const Rgb lo = tint(0.05), mid = tint(0.5), hi = tint(0.98);
  report("tint: red low, amber mid, green high", lo.r > 0.85f && lo.g < 0.4f && std::fabs(mid.g - 0.70f) < 0.02f && hi.g > 0.7f && hi.r < 0.4f);
  report("percent", percent(0.934) == "93%" && percent(1.2) == "100%" && percent(-1) == "0%");

  std::vector<Token> many{Token{"Paris", 0.0, {}}, Token{" is", -0.1, {}}, Token{" perhaps", -2.1, {Alt{" the", -0.3}}}, Token{" big", -0.9, {}}};
  const Stats s = stats(many);
  report("stats: count, mean, low count, lowest", s.count == 4 && s.low == 2 && s.lowest == 2 && std::fabs(s.lowestP - std::exp(-2.1)) < 1e-9 && s.mean > 0.5 && s.mean < 0.8);
  report("statsLine", statsLine(s, many) == "mean " + percent(s.mean) + " over 4 tokens - 2 under 50% - lowest ' perhaps' 12%" && statsLine(Stats{}, {}) == "no token probabilities");
  report("tooltip lists the token and the alternatives that differ", tooltip(many[2]) == "' perhaps'  12%\nalternatives:\n  ' the'  74%" && tooltip(tokens[1]) == "' moon'  30%" && tooltip(tokens[0]).find("' let'  0%") != std::string::npos);
  report("aligned: exact, prefix while streaming, mismatch", aligned(many, "Paris is perhaps big") && aligned(many, "Paris is perhaps big and old") && !aligned(many, "Paris was") && !aligned({}, "x"));

  Json payload{{"model", "m"}, {"messages", Json::array()}, {"logprobs", true}, {"top_logprobs", 3}};
  const Json oa = backends::openaiRequest(payload);
  report("openaiRequest passes logprobs and top_logprobs through", oa["logprobs"] == true && oa["top_logprobs"] == 3 && !backends::anthropicRequest(payload).contains("logprobs"));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
