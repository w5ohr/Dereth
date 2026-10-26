// `Open WebUI --memorytest`: offline checks of memory-proposal parsing and filtering.
#include <cstdio>
#include <string>

#include "core/memoryprop.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runMemoryPropSelfTest() {
  using namespace memoryprop;
  printf("Open WebUI desktop memory-proposals self-test\n\n");

  std::vector<std::string> p = parse("{\"memories\": [\"I prefer short answers.\", \"My studio is in Austin.\"]}");
  report("parse: object form", p.size() == 2 && p[0] == "I prefer short answers." && p[1] == "My studio is in Austin.");
  p = parse("<think>hmm</think>\n```json\n[\"I run a Pilates studio\"]\n```");
  report("parse: think prefix, fence, bare array", p.size() == 1 && p[0] == "I run a Pilates studio");
  p = parse("Sure! Here you go:\n{\"memories\": [{\"memory\": \"I use Neovim\"}, {\"text\": \"My dog is called Pip\"}]}\nDone.");
  report("parse: prose around the JSON, object items", p.size() == 2 && p[0] == "I use Neovim" && p[1] == "My dog is called Pip");
  p = parse("- I like British spelling\n- 2. I am vegetarian\nnothing else");
  report("parse: bullet fallback with numbering stripped", p.size() == 2 && p[0] == "I like British spelling" && p[1] == "I am vegetarian");
  report("parse: empty list", parse("{\"memories\": []}").empty() && parse("Nothing to remember.").empty());

  report("normalize drops filler and punctuation", normalize("I prefer short, bullet-point answers!") == "short bullet point answers");
  report("similar: same fact in different words", similar("I prefer concise answers in bullet points", "The user prefers bullet-point concise answers") &&
                                                       similar("My studio is in Austin", "my studio is in austin.") &&
                                                       !similar("I prefer short answers", "My dog is called Pip"));
  report("similar: containment", similar("I run a Pilates studio in Austin", "I run a Pilates studio in Austin, Texas, since 2019"));

  std::vector<std::string> f = filter({"I prefer short answers", "i prefer short answers.", "ok", "My studio is in Austin",
                                       "I am allergic to peanuts", "I like tea", std::string(300, 'x')},
                                      {"The user prefers short answers"}, {"I like tea"}, 3);
  report("filter: existing, skipped, self-duplicates, too short / long dropped; cap 3",
         f.size() == 2 && f[0] == "My studio is in Austin" && f[1] == "I am allergic to peanuts",
         std::to_string(f.size()) + (f.empty() ? "" : " first=" + f[0]));
  const std::string prompt = userPrompt("I run a studio", "Nice", {"I like tea"});
  report("userPrompt carries both sides and the existing list", prompt.find("User said:\nI run a studio") != std::string::npos &&
                                                                    prompt.find("- I like tea") != std::string::npos);
  report("systemPrompt asks for JSON with up to 3", systemPrompt().find("{\"memories\"") != std::string::npos && systemPrompt().find("0 to 3") != std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
