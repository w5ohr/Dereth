// `Open WebUI --recipetest`: offline checks of recipe JSON and step expansion.
#include <cstdio>
#include <string>

#include "core/recipes.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runRecipeSelfTest() {
  using namespace recipes;
  printf("Open WebUI desktop recipe self-test\n\n");

  Recipe r;
  r.id = "t";
  r.name = "Test";
  r.steps = {{"Summarise {{clipboard}} for {{input:Audience|my team}}", "qwen3:8b"},
             {"Now shorten:\n{{prev}}\n(for {{input:Audience}}, written on {{date}})", ""},
             {"Compare {{step:1}} with {{STEP: 2}} and {{prev}}", ""}};
  const Json j = r.toJson();
  const Recipe back = Recipe::fromJson(j);
  report("json round-trip", back.id == "t" && back.steps.size() == 3 && back.steps[0].model == "qwen3:8b" && back.newChatPerRun);
  report("string steps accepted", Recipe::fromJson(Json{{"id", "s"}, {"steps", Json::array({"a", "b"})}}).steps.size() == 2);

  const auto ins = inputs(r);
  report("inputs collected once across steps", ins.size() == 1 && ins[0].arg == "Audience" && ins[0].def == "my team");

  promptvars::Context ctx;
  ctx.clipboard = [] { return std::string("CLIP {{prev}}"); };
  ctx.inputs["Audience"] = "the board";
  ctx.now = 1700000000;
  std::vector<std::string> replies;
  std::string s = expandStep(r.steps[0].prompt, replies, ctx);
  report("step 1: clipboard + input, clipboard text not re-expanded", s == "Summarise CLIP {{prev}} for the board", s);
  replies.push_back("REPLY ONE {{date}}");
  s = expandStep(r.steps[1].prompt, replies, ctx);
  report("step 2: prev spliced, its braces untouched, date filled",
         s.rfind("Now shorten:\nREPLY ONE {{date}}\n(for the board, written on ", 0) == 0 && s.find("{{date}})") == std::string::npos, s);
  replies.push_back("REPLY TWO");
  s = expandStep(r.steps[2].prompt, replies, ctx);
  report("step 3: step:N and prev", s == "Compare REPLY ONE {{date}} with REPLY TWO and REPLY TWO", s);
  s = expandStep("{{prev}} and {{step:9}}", {}, ctx);
  report("missing replies expand to nothing", s == " and ", "[" + s + "]");
  report("examples are well-formed", examples().size() == 3 && !examples()[1].steps.empty() && inputs(examples()[1]).size() == 1);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
