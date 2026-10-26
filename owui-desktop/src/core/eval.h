// Model evaluation (#149): run a saved prompt set across models, grade each
// answer with a judge model or by hand, keep every score and trend them per
// model. The pure parts: prompt sets and scores as JSON, the judge prompt
// and its answer parser, the latest-run matrix, trends and CSV. App-free;
// --evaltest covers it.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace eval {

using Json = nlohmann::json;

struct Prompt {
  std::string id, name, text;
  std::string expected;   // reference answer or rubric for the judge (optional)
};
struct PromptSet {
  std::string id, name;
  std::vector<Prompt> prompts;
};
std::vector<PromptSet> setsFromJson(const Json& j);
Json setsToJson(const std::vector<PromptSet>& sets);
PromptSet defaultSet();   // five starter prompts

struct Score {
  std::string runId, setId, promptId, model;
  std::string answer;
  std::string judge;      // judge model, "" when graded by hand only
  std::string reason;     // the judge's sentence
  double seconds = 0.0;
  int grade = -1;         // 0..10 from the judge, -1 = not graded
  int thumbs = 0;         // -1 / 0 / 1, set by hand; overrides the grade
  int64_t at = 0;
  std::string error;      // the model call failed
};
Json scoreToJson(const Score& s);
Score scoreFromJson(const Json& j);
std::vector<Score> scoresFromJsonl(const std::string& text);
int effectiveGrade(const Score& s);   // thumbs up = 10, down = 0, else the judge's grade (-1 when none)

std::string judgeSystem();
std::string judgeUser(const Prompt& prompt, const std::string& answer);
std::string stripThinking(const std::string& text);   // drops <think>...</think>
// {"score": 7, "reason": "..."} anywhere in the text (fenced or not), or "7/10" / "Score: 7".
bool parseJudge(const std::string& text, int* grade, std::string* reason);

// promptId -> model -> newest score, for a set.
std::map<std::string, std::map<std::string, Score>> latest(const std::vector<Score>& scores, const std::string& setId);
struct Trend {
  std::string model;
  int runs = 0;                   // distinct runs with a graded answer
  double mean = 0.0;              // mean of the per-run means
  double last = 0.0;              // newest run's mean
  double meanSeconds = 0.0;
  std::vector<double> history;    // per-run means, oldest first
};
std::vector<Trend> trends(const std::vector<Score>& scores, const std::string& setId);   // best mean first
std::string csv(const std::vector<Score>& scores);

}  // namespace eval
