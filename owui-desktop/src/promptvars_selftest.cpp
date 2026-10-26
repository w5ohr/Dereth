// `Open WebUI --varstest`: offline checks of prompt-variable parsing and
// expansion with a fake context. No server needed.
#include <cstdio>
#include <map>
#include <string>

#include "core/promptvars.h"

namespace {

int gPassed = 0, gFailed = 0;

void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

}  // namespace

int runPromptVarsSelfTest() {
  using namespace promptvars;
  printf("Open WebUI desktop prompt-variables self-test\n\n");

  printf("parse\n");
  auto vars = parse("Summarise {{clipboard}} for {{ input:Audience | my team }} on {{DATE}} from {{file:~/notes.md}} {{nope}}");
  report("count", vars.size() == 5, std::to_string(vars.size()));
  report("names", vars.size() == 5 && vars[0].name == "clipboard" && vars[1].name == "input" &&
                      vars[2].name == "date" && vars[3].name == "file" && vars[4].name == "nope");
  report("input arg and default", vars.size() > 1 && vars[1].arg == "Audience" && vars[1].def == "my team");
  report("file arg", vars.size() > 3 && vars[3].arg == "~/notes.md");
  report("raw kept", vars.size() > 1 && vars[1].raw == "{{ input:Audience | my team }}");
  report("open webui aliases", parse("{{CURRENT_DATE}} {{USER_NAME}} {{CLIPBOARD}}").size() == 3 &&
                                   parse("{{CURRENT_DATE}}")[0].name == "date" &&
                                   parse("{{USER_NAME}}")[0].name == "user");
  report("inputs deduped in order",
         inputs(parse("{{input:B}} {{input:A}} {{input:B}}")).size() == 2 &&
             inputs(parse("{{input:B}} {{input:A}} {{input:B}}"))[0].arg == "B");
  report("hasVariables true", hasVariables("x {{date}} y"));
  report("hasVariables false for unknown", !hasVariables("x {{whatever}} y"));
  report("hasVariables false for plain braces", !hasVariables("json {\"a\": {}} and {{ }}"));

  printf("\nexpand\n");
  Context ctx;
  ctx.clipboard = [] { return std::string("CLIP"); };
  ctx.selection = [] { return std::string(); };
  std::map<std::string, std::string> files = {{"/tmp/a.txt", "file body"}, {"/tmp/bin", std::string("ab\0cd", 5)},
                                              {"/tmp/big", std::string(300 * 1024, 'x')}};
  ctx.file = [&](const std::string& p) { auto it = files.find(p); return it == files.end() ? std::string() : it->second; };
  ctx.user = "Ann";
  ctx.model = "qwen3:8b";
  ctx.chatTitle = "Planets";
  ctx.now = 1761350400;   // 2025-10-25 00:00:00 UTC

  Expansion e = expand("[{{clipboard}}] [{{selection}}] [{{user}}] [{{model}}] [{{title}}]", ctx);
  report("clipboard/user/model/title", e.text == "[CLIP] [] [Ann] [qwen3:8b] [Planets]", e.text);
  report("notes mention clipboard and selection", e.notes.size() == 2 && e.notes[0] == "clipboard: 4 B" &&
                                                      e.notes[1] == "selection: nothing selected");
  e = expand("{{date}}|{{time}}|{{weekday}}", ctx);
  report("date-ish filled (local time)", e.text.size() >= 20 && e.text.find("{{") == std::string::npos, e.text);
  e = expand("{{file:/tmp/a.txt}}", ctx);
  report("file inlined", e.text == "file body" && e.notes.size() == 1 && e.notes[0] == "file /tmp/a.txt: 9 B", e.text);
  e = expand("{{file:/tmp/missing}}", ctx);
  report("missing file leaves nothing and a note", e.text.empty() && !e.notes.empty() &&
                                                       e.notes[0].find("not found") != std::string::npos);
  e = expand("{{file:/tmp/bin}}", ctx);
  report("binary file skipped", e.text.find("binary file") != std::string::npos);
  e = expand("{{file:/tmp/big}}", ctx);
  report("big file truncated", e.text.size() < 210 * 1024 && e.text.find("truncated") != std::string::npos);
  e = expand("Hi {{input:Name|stranger}}, {{input:Name}}!", ctx);
  report("input default", e.text == "Hi stranger, !", e.text);
  ctx.inputs["Name"] = "Bo";
  e = expand("Hi {{input:Name|stranger}}, {{input:Name}}!", ctx);
  report("input value everywhere", e.text == "Hi Bo, Bo!", e.text);
  e = expand("keep {{unknown}} and {{ x:y }}", ctx);
  report("unknown untouched", e.text == "keep {{unknown}} and {{ x:y }}", e.text);
  ctx.clipboard = [] { return std::string("{{date}}"); };
  e = expand("{{clipboard}}", ctx);
  report("clipboard contents not re-expanded", e.text == "{{date}}", e.text);
  e = expand("no variables here", ctx);
  report("plain text unchanged", e.text == "no variables here" && e.notes.empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
