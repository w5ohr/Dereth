// `Open WebUI --evaltest`: offline checks of model evaluation.
#include <cstdio>
#include <string>

#include "core/eval.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
eval::Score score(const std::string& run, const std::string& prompt, const std::string& model, int grade, int64_t at, double seconds = 1.0, int thumbs = 0) {
  eval::Score s;
  s.runId = run;
  s.setId = "starter";
  s.promptId = prompt;
  s.model = model;
  s.grade = grade;
  s.at = at;
  s.seconds = seconds;
  s.thumbs = thumbs;
  return s;
}
}  // namespace

int runEvalSelfTest() {
  using namespace eval;
  printf("Open WebUI desktop model-evaluation self-test\n\n");

  const PromptSet d = defaultSet();
  report("defaultSet: five prompts with ids, text and rubrics", d.prompts.size() == 5 && d.id == "starter" && !d.prompts[0].expected.empty() && d.prompts[4].id == "starter-5");
  std::vector<PromptSet> sets = setsFromJson(setsToJson({d}));
  report("sets round-trip through JSON; junk skipped", sets.size() == 1 && sets[0].prompts.size() == 5 && sets[0].prompts[2].name == "Code" &&
                                                          setsFromJson(Json::array({Json{{"name", "x"}, {"prompts", Json::array({Json{{"text", ""}}, Json{{"text", "hi"}}})}}}))[0].prompts.size() == 1 &&
                                                          setsFromJson(Json("nope")).empty());

  Score s = score("r1", "p1", "m", 7, 100, 2.5);
  s.answer = "a \"quoted\" answer, with commas";
  s.reason = "fine";
  Score back = scoreFromJson(scoreToJson(s));
  report("score round-trip; thumbs override the grade", back.answer == s.answer && back.grade == 7 && back.seconds == 2.5 && back.at == 100 && effectiveGrade(back) == 7 &&
                                                            effectiveGrade(score("r", "p", "m", 3, 1, 1, 1)) == 10 && effectiveGrade(score("r", "p", "m", 9, 1, 1, -1)) == 0 && effectiveGrade(score("r", "p", "m", -1, 1)) == -1);
  const std::vector<Score> lines = scoresFromJsonl(scoreToJson(s).dump() + "\n\n{bad\n" + scoreToJson(score("r2", "p2", "m", 4, 200)).dump() + "\n");
  report("scoresFromJsonl skips blank and broken lines", lines.size() == 2 && lines[1].promptId == "p2");

  report("judge prompts carry the task, rubric and answer, and ask for JSON", judgeSystem().find("\"score\"") != std::string::npos && judgeUser(d.prompts[0], "3h25m").find("Reference answer or rubric:\n3 hours 25 minutes") != std::string::npos &&
                                                                                 judgeUser(d.prompts[0], "").find("(empty)") != std::string::npos);
  int g = -1;
  std::string reason;
  report("parseJudge: plain JSON", parseJudge("{\"score\": 8, \"reason\": \"close\"}", &g, &reason) && g == 8 && reason == "close");
  report("parseJudge: fenced JSON after a think block, string score, clamped", parseJudge("<think>hmm\nthinking</think>\n```json\n{\"score\": \"12\", \"reason\": \"too generous\"}\n```", &g, &reason) && g == 10 && reason == "too generous");
  report("parseJudge: prose fallbacks", parseJudge("I would give this 6/10 because it misses the surcharge.", &g, &reason) && g == 6 && parseJudge("Score: 3 - wrong units", &g, nullptr) && g == 3 &&
                                             !parseJudge("No verdict here", &g, nullptr));
  report("stripThinking", stripThinking("<think>a</think> answer <think>b</think>") == "answer" && stripThinking("<think>never closed") == "" && stripThinking("plain") == "plain");

  std::vector<Score> all{score("r1", "p1", "A", 6, 100, 1.0), score("r1", "p2", "A", 8, 101, 3.0), score("r1", "p1", "B", 9, 102), score("r1", "p2", "B", 9, 103),
                         score("r2", "p1", "A", 8, 200, 2.0), score("r2", "p2", "A", 10, 201, 2.0), score("r2", "p1", "B", -1, 202), score("r3", "p1", "A", 5, 300, 1.0, 1)};
  all.back().setId = "other";
  const auto m = latest(all, "starter");
  report("latest: newest score per prompt and model, other sets ignored", m.size() == 2 && m.at("p1").at("A").runId == "r2" && m.at("p1").at("B").runId == "r2" && m.at("p1").at("B").grade == -1 &&
                                                                              m.at("p2").at("B").grade == 9 && latest(all, "other").size() == 1);
  const std::vector<Trend> t = trends(all, "starter");
  report("trends: per-run means, best mean first, last run, mean seconds, ungraded runs skipped",
         t.size() == 2 && t[0].model == "B" && t[0].runs == 1 && t[0].mean == 9.0 && t[1].model == "A" && t[1].runs == 2 && t[1].history.size() == 2 && t[1].history[0] == 7.0 && t[1].history[1] == 9.0 &&
             t[1].mean == 8.0 && t[1].last == 9.0 && t[1].meanSeconds == 2.0,
         t.empty() ? "no trends" : t[1].model + " mean " + std::to_string(t[1].mean));
  const std::string c = csv({s});
  report("csv: header, quoted answer, effective grade", c.find("at,run,set,prompt,model,grade,thumbs,effective,seconds") == 0 && c.find(",7,0,7,2.5") != std::string::npos &&
                                                            c.find("\"a \"\"quoted\"\" answer, with commas\"") != std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
