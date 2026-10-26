// `Open WebUI --foldertest`: offline checks of smart folders and auto-tagging.
#include <cstdio>
#include <string>

#include "core/smartfolders.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runSmartFoldersSelfTest() {
  using namespace smartfolders;
  printf("Open WebUI desktop smart-folders self-test\n\n");

  const Json json = Json::array({Json{{"name", "SQL work"}, {"folder", "Databases"}, {"tag", "SQL"}, {"titleContains", "sql"}},
                                 Json{{"folder", " Archive "}, {"olderThanDays", 30}},
                                 Json{{"tag", "base-model"}, {"modelContains", "base"}},
                                 Json{{"name", "no action"}, {"titleContains", "x"}},
                                 Json{{"tag", "fresh"}, {"newerThanDays", 2}, {"enabled", false}}});
  std::vector<Rule> rules = rulesFromJson(json);
  report("rulesFromJson: ids, default names, trimmed folder, normalized tags, rules without an action dropped", rules.size() == 4 && !rules[0].id.empty() && rules[0].tag == "sql" && rules[1].folder == "Archive" &&
                                                                                                                   rules[1].name == "file into Archive" && rules[2].name == "tag base-model" && !rules[3].enabled);
  report("rules round-trip", rulesFromJson(rulesToJson(rules)).size() == 4 && rulesFromJson(rulesToJson(rules))[1].olderThanDays == 30 && needsChatBody(rules) && !needsChatBody({rules[0], rules[1]}));
  report("describe", describe(rules[0]) == "title contains \"sql\" -> folder Databases, tag #sql" && describe(rules[1]) == "older than 30 days -> folder Archive");

  const int64_t now = 1700000000;
  ChatFacts sqlChat{"c1", "SQL window functions", "", {"qwen3:8b"}, {}, now - 3600, "how do window functions work"};
  ChatFacts oldChat{"c2", "Holiday plans", "f-old", {"llama3"}, {"travel"}, now - 40 * 86400, ""};
  ChatFacts baseChat{"c3", "Winch greasing", "", {"qwen2.5-coder:3b-base"}, {"base-model"}, now - 100, "when are the winches greased"};
  report("matches: title, age, model, disabled", matches(rules[0], sqlChat, now) && !matches(rules[0], oldChat, now) && matches(rules[1], oldChat, now) && !matches(rules[1], sqlChat, now) &&
                                                     matches(rules[2], baseChat, now) && !matches(rules[2], sqlChat, now) && !matches(rules[3], ChatFacts{"c", "x", "", {}, {}, now, ""}, now));
  Rule tagged;
  tagged.tag = "t";
  tagged.hasTag = "Travel";
  tagged.enabled = true;
  Rule text;
  text.tag = "t";
  text.textContains = "WINDOW";
  text.enabled = true;
  report("matches: hasTag (normalized) and text, unknown age never 'older'", matches(tagged, oldChat, now) && !matches(tagged, sqlChat, now) && matches(text, sqlChat, now) &&
                                                                                 !matches(rules[1], ChatFacts{"c", "t", "", {}, {}, 0, ""}, now));

  const std::map<std::string, std::string> folderIds{{"archive", "f-old"}, {"databases", "f-db"}};
  std::vector<Action> actions = plan(rules, {sqlChat, oldChat, baseChat}, now, folderIds);
  report("plan: move + tag for sql chat, old chat already filed so nothing, base chat already tagged so nothing", actions.size() == 1 && actions[0].chatId == "c1" && actions[0].folder == "Databases" &&
                                                                                                                      actions[0].tag == "sql" && actions[0].ruleName == "SQL work",
         std::to_string(actions.size()) + " actions");
  ChatFacts oldUnfiled = oldChat;
  oldUnfiled.folderId = "";
  oldUnfiled.tags.clear();
  ChatFacts baseUntagged = baseChat;
  baseUntagged.tags.clear();
  actions = plan(rules, {oldUnfiled, baseUntagged}, now, folderIds);
  report("plan: first moving rule wins, tags from every matching rule", actions.size() == 2 && actions[0].folder == "Archive" && actions[0].tag.empty() && actions[1].folder.empty() && actions[1].tag == "base-model" &&
                                                                            summary(actions) == "1 chat moved, 1 tag added (2 chats touched)" && summary({}).find("Nothing to do") == 0);

  report("normalizeTag", normalizeTag(" #Unit Tests ") == "unit-tests" && normalizeTag("C++/STL") == "c++-stl" && normalizeTag("---") == "" && normalizeTag(std::string(40, 'a')).size() == 24);
  report("parseTags: JSON array, think block, dedupe, cap, generic words dropped", parseTags("<think>hm</think>[\"Python\", \"unit tests\", \"python\", \"chat\", \"pytest\", \"ci\"]") == std::vector<std::string>({"python", "unit-tests", "pytest"}) &&
                                                                                       parseTags("Tags: sql, Window Functions\nmore text") == std::vector<std::string>({"sql", "window-functions"}) && parseTags("").empty() &&
                                                                                       parseTags("[]\nregex").empty());
  report("tag prompts", tagSystemPrompt().find("JSON array") != std::string::npos && tagUserPrompt("T", "u", "r").find("Title: T\n\nUser:\nu\n\nAssistant:\nr") == 0 && tagUserPrompt("", "u", "").find("(none)") != std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
