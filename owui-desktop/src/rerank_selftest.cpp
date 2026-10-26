// `Open WebUI --reranktest`: offline checks of the local reranker.
#include <cstdio>
#include <string>

#include "core/rerank.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runRerankSelfTest() {
  using namespace rerank;
  printf("Open WebUI desktop local-reranker self-test\n\n");

  const Prefs d = fromJson(Json::object());
  report("defaults: off, keep 4, min score 3", !d.enabled && d.keep == 4 && d.minScore == 3);
  const Prefs p = fromJson(Json{{"localRerank", Json{{"enabled", true}, {"keep", 99}, {"minScore", -4}}}});
  report("fromJson clamps and round-trips", p.enabled && p.keep == 12 && p.minScore == 0 && fromJson(Json{{"localRerank", toJson(p)}}).keep == 12);

  const std::vector<Candidate> cands{{"1", "winches.md", "The winches are greased every spring by the keeper."}, {"2", "visitors.md", "Visitors may climb the tower on Sundays."},
                                     {"3", "foghorn.py", std::string(900, 'x')}};
  const std::string prompt = userPrompt("when are the winches greased?", cands, 100);
  report("userPrompt: question, every passage with id and title, long text cut", prompt.find("Question: when are the winches greased?") == 0 && prompt.find("[1] winches.md\nThe winches") != std::string::npos &&
                                                                                      prompt.find("[3] foghorn.py\n") != std::string::npos && prompt.find(std::string(101, 'x')) == std::string::npos && prompt.find("...") != std::string::npos &&
                                                                                      systemPrompt().find("JSON") != std::string::npos);

  std::vector<Scored> s = parse("<think>hm</think>\n```json\n[{\"id\": \"2\", \"score\": 2}, {\"id\": 1, \"score\": 9}, {\"id\": \"9\", \"score\": 5}, {\"id\": \"3\", \"score\": \"4\"}]\n```", cands);
  report("parse: objects with string or number ids, string scores, unknown ids dropped, think block ignored", s.size() == 3 && s[0].id == "2" && s[0].score == 2 && s[1].id == "1" && s[1].score == 9 && s[2].id == "3" && s[2].score == 4);
  s = parse("[9, 2, 11]", cands);
  report("parse: positional array maps onto the candidates, clamped", s.size() == 3 && s[0].id == "1" && s[0].score == 9 && s[2].id == "3" && s[2].score == 10);
  s = parse("Scores:\n1: 8\n[2] - 3\n3 = 0\n", cands);
  report("parse: id: score lines", s.size() == 3 && s[0].score == 8 && s[1].score == 3 && s[2].score == 0 && parse("no scores here", cands).empty());

  Prefs prefs;
  prefs.keep = 2;
  prefs.minScore = 3;
  const std::vector<Scored> scored{{"1", 9}, {"2", 3}, {"3", 1}, {"4", 7}};
  report("order: by score, under the floor dropped, capped", order(scored, prefs) == std::vector<std::string>({"1", "4"}) && [&] { Prefs all = prefs; all.keep = 12; return order(scored, all) == std::vector<std::string>({"1", "4", "2"}); }() &&
                                                                 order({}, prefs).empty());
  report("summary", summary(scored) == "4 scored: 9, 7, 3, 1" && summary({}) == "nothing scored");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
