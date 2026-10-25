// `Open WebUI --outputtest`: offline checks of the grammar / tool-choice controls.
#include <cstdio>
#include <string>

#include "core/backends.h"
#include "core/outputctl.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runOutputSelfTest() {
  using namespace outputctl;
  printf("Open WebUI desktop output-controls self-test\n\n");

  const Controls d = fromJson(Json::object());
  report("defaults: free text, auto tools, inactive", d.mode == "free" && d.toolChoice == "auto" && d.grammar.empty() && !active(d));
  const Controls c = fromJson(Json{{"outputControls", Json{{"mode", "grammar"}, {"grammar", "root ::= \"a\""}, {"toolChoice", "tool:calc__add"}}}});
  const Controls bad = fromJson(Json{{"outputControls", Json{{"mode", "xml"}, {"toolChoice", "tool:"}}}});
  report("fromJson validates mode and choice; round-trips", c.mode == "grammar" && c.toolChoice == "tool:calc__add" && active(c) && bad.mode == "free" && bad.toolChoice == "auto" &&
                                                                 fromJson(Json{{"outputControls", toJson(c)}}).grammar == c.grammar);

  const std::string good =
      "# a list of words\n"
      "root ::= \"[\" ws item (\",\" ws item)* \"]\"\n"
      "item ::= \"\\\"\" [a-z]+ \"\\\"\"   # quoted word\n"
      "ws   ::= [ \\t\\n]*\n";
  report("validateGrammar accepts a real grammar (comments, classes, escapes) and counts rules", validateGrammar(good).empty() && ruleCount(good) == 3, validateGrammar(good));
  report("validateGrammar: no root", validateGrammar("item ::= \"a\"").find("root") != std::string::npos);
  report("validateGrammar: undefined reference", validateGrammar("root ::= item").find("`item`") != std::string::npos);
  report("validateGrammar: unclosed string, unbalanced parens, bad line", validateGrammar("root ::= \"a").find("unclosed") != std::string::npos &&
                                                                              validateGrammar("root ::= (\"a\"").find("parentheses") != std::string::npos &&
                                                                              validateGrammar("hello world").find("line 1") != std::string::npos);
  report("validateGrammar: a continuation line belongs to the previous rule", validateGrammar("root ::= \"a\"\n  | \"b\"").empty());

  report("openaiToolChoice shapes", openaiToolChoice("auto") == "auto" && openaiToolChoice("none") == "none" && openaiToolChoice("required") == "required" &&
                                        openaiToolChoice("tool:calc__add")["function"]["name"] == "calc__add");
  report("anthropicToolChoice shapes", anthropicToolChoice("auto")["type"] == "auto" && anthropicToolChoice("required")["type"] == "any" && anthropicToolChoice("none").is_null() &&
                                           anthropicToolChoice("tool:x")["name"] == "x");

  Controls n;
  n.toolChoice = "required";
  n.mode = "json";
  const std::string text = nudge(n, {"calc__add", "web__fetch"});
  report("nudge names the tools and asks for JSON; empty when nothing applies", text.find("calc__add, web__fetch") != std::string::npos && text.find("single JSON object") != std::string::npos &&
                                                                                      nudge(Controls{}, {"x"}).empty() && nudge(fromJson(Json{{"outputControls", Json{{"toolChoice", "tool:calc__add"}}}}), {}).find("`calc__add`") != std::string::npos);

  Json payload{{"model", "m"}, {"messages", Json::array()}, {"tools", Json::array({Json{{"type", "function"}}})}};
  Json p1 = payload;
  applyToOllamaPayload(p1, n);
  Controls none;
  none.toolChoice = "none";
  Json p2 = payload;
  applyToOllamaPayload(p2, none);
  Json p3 = payload;
  applyToOllamaPayload(p3, c);
  report("applyToOllamaPayload: json format + tool_choice; none drops tools; grammar + forced tool", p1["format"] == "json" && p1["tool_choice"] == "required" && !p2.contains("tools") && !p2.contains("tool_choice") &&
                                                                                                        p3["grammar"] == c.grammar && p3["tool_choice"]["function"]["name"] == "calc__add");

  const Json oa = backends::openaiRequest(p3);
  Json p4 = p1;
  p4.erase("tools");
  report("openaiRequest carries grammar and tool_choice through; json format -> json_object", oa["grammar"] == c.grammar && oa["tool_choice"]["function"]["name"] == "calc__add" &&
                                                                                                    backends::openaiRequest(p1)["response_format"]["type"] == "json_object");
  Json anth = payload;
  anth["tools"] = Json::array({Json{{"type", "function"}, {"function", Json{{"name", "calc__add"}, {"parameters", Json{{"type", "object"}}}}}}});
  Json a1 = anth;
  applyToOllamaPayload(a1, n);
  Json a2 = anth;
  applyToOllamaPayload(a2, c);
  report("anthropicRequest maps tool_choice (required -> any, forced -> tool)", backends::anthropicRequest(a1)["tool_choice"]["type"] == "any" && backends::anthropicRequest(a2)["tool_choice"]["name"] == "calc__add" &&
                                                                                    !backends::anthropicRequest(a2).contains("grammar"));
  report("summary", summary(n) == "JSON, a tool first" && summary(c) == "GBNF (1 rule), tool: calc__add" && summary(none) == "no tools" && summary(Controls{}).empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
