// Logprob view (#78): token log-probabilities a backend streams back,
// turned into a per-token confidence, a colour, alternatives for a tooltip
// and a summary. The pure parts; app-free; --logprobtest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace logprobs {

using Json = nlohmann::json;

struct Prefs {
  bool enabled = false;   // ask backends for logprobs on every request
  int top = 3;            // alternatives per token, 1..8
};
Prefs prefsFromJson(const Json& extra);   // extra["logprobs"]
Json prefsToJson(const Prefs& p);

struct Alt {
  std::string token;
  double logprob = 0.0;
};
struct Token {
  std::string token;
  double logprob = 0.0;
  std::vector<Alt> alts;   // top alternatives, the chosen token included when the backend lists it
};

// Entries as Ollama's chunk["logprobs"] and OpenAI's choice.logprobs.content
// list them: {token, logprob, top_logprobs:[{token, logprob}]}.
std::vector<Token> fromEntries(const Json& list);
std::vector<Token> fromOpenAIChoice(const Json& choice);   // choice["logprobs"]["content"]
// Compact storage on a message: [[token, logprob, [[alt, logprob], ...]], ...]
Json toJson(const std::vector<Token>& tokens);
std::vector<Token> fromJson(const Json& compact);

double confidence(double logprob);   // exp(logprob), clamped to [0, 1]
struct Rgb {
  float r, g, b;
};
Rgb tint(double p);                  // red below 0.5, amber around it, green above 0.8
std::string percent(double p);       // "93%"

struct Stats {
  int count = 0;
  double mean = 0.0;      // mean confidence
  int low = 0;            // tokens under 50 %
  int lowest = -1;        // index of the least confident token
  double lowestP = 1.0;
};
Stats stats(const std::vector<Token>& tokens);
std::string statsLine(const Stats& s, const std::vector<Token>& tokens);
std::string tooltip(const Token& t);   // the token, its confidence and each alternative
// The tokens spell the text (or a prefix of it while streaming), so a token
// view can stand in for the rendered text.
bool aligned(const std::vector<Token>& tokens, const std::string& text);

}  // namespace logprobs
