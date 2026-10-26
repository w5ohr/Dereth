// `Open WebUI --agenttest`: offline checks of the agent loop's plan and
// verdict parsing. The loop itself runs live in the GUI against a server.
#include <cstdio>
#include <string>

#include "agents/agent_loop.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runAgentSelfTest() {
  using namespace agents;
  printf("Open WebUI desktop agent self-test\n\n");

  printf("plan\n");
  auto steps = parsePlan(R"({"steps": ["Read the file", "Count the words", "Report"]})", 6);
  report("json object", steps.size() == 3 && steps[1] == "Count the words");
  steps = parsePlan(R"(["a", "b"])", 6);
  report("json array", steps.size() == 2 && steps[0] == "a");
  steps = parsePlan(R"({"steps": [{"title": "x"}, {"step": "y"}, {"description": "z"}]})", 6);
  report("object items", steps.size() == 3 && steps[2] == "z");
  steps = parsePlan("Sure:\n1. First thing\n2) Second thing\n- Third thing\n", 6);
  report("numbered prose fallback", steps.size() == 3 && steps[0] == "First thing" && steps[2] == "Third thing",
         std::to_string(steps.size()));
  steps = parsePlan(R"({"steps": ["1","2","3","4","5","6","7","8"]})", 3);
  report("capped at maxSteps", steps.size() == 3);
  steps = parsePlan("I cannot plan this.", 6);
  report("nothing parsable -> empty", steps.empty());
  steps = parsePlan("```json\n{\"steps\": [\"fenced\"]}\n```", 6);
  report("fenced json", steps.size() == 1 && steps[0] == "fenced");

  printf("\nverdict\n");
  bool done = false;
  std::string feedback;
  report("json done", parseVerdict(R"({"done": true, "feedback": ""})", &done, &feedback) && done);
  report("json not done with feedback",
         parseVerdict(R"({"done": false, "feedback": "no file was written"})", &done, &feedback) && !done &&
             feedback == "no file was written");
  report("string boolean", parseVerdict(R"({"done": "true"})", &done, &feedback) && done);
  report("prose yes", parseVerdict("Yes, it is complete.", &done, &feedback) && done);
  report("prose no", parseVerdict("No - the count is missing.", &done, &feedback) && !done && !feedback.empty());
  report("unparsable", !parseVerdict("Hmm.", &done, &feedback));

  printf("\nschema\n");
  const Json schema = planSchema(4);
  report("plan schema shape", schema["properties"]["steps"]["maxItems"] == 4 &&
                                  schema["required"][0] == "steps");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
