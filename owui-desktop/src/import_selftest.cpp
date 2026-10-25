// `Open WebUI --importtest`: offline checks of the ChatGPT and Claude
// export converters.
#include <cstdio>
#include <string>

#include "core/importers.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runImportSelfTest() {
  using namespace importers;
  printf("Open WebUI desktop import self-test\n\n");

  // ChatGPT: root -> system (hidden) -> user -> assistant(a) / assistant(b); current = b.
  const Json chatgpt = Json::parse(R"json([{
    "title": "Trip planning", "create_time": 1700000000.5, "current_node": "b",
    "mapping": {
      "root": {"id": "root", "message": null, "parent": null, "children": ["sys"]},
      "sys": {"id": "sys", "parent": "root", "children": ["u1"],
              "message": {"author": {"role": "system"}, "content": {"content_type": "text", "parts": [""]},
                          "metadata": {"is_visually_hidden_from_conversation": true}}},
      "u1": {"id": "u1", "parent": "sys", "children": ["a", "b"],
             "message": {"author": {"role": "user"}, "create_time": 1700000001, "content": {"content_type": "text", "parts": ["Plan a trip"]}}},
      "a": {"id": "a", "parent": "u1", "children": [],
            "message": {"author": {"role": "assistant"}, "content": {"content_type": "text", "parts": ["Old answer"]}, "metadata": {"model_slug": "gpt-4"}}},
      "b": {"id": "b", "parent": "u1", "children": [],
            "message": {"author": {"role": "assistant"}, "content": {"content_type": "code", "parts": ["print(1)"]}, "metadata": {"model_slug": "gpt-4o"}}}
    }},
    {"title": "Empty", "mapping": {"r": {"id": "r", "message": null, "parent": null, "children": []}}}
  ])json");
  report("detect chatgpt", detect(chatgpt) == Source::ChatGPT);
  auto out = convert(chatgpt, Source::ChatGPT, "qwen3:8b");
  report("one conversation (the empty one skipped)", out.size() == 1, std::to_string(out.size()));
  if (out.size() == 1) {
    const Imported& c = out[0];
    report("title, time, count", c.title == "Trip planning" && c.createdAt == 1700000000 && c.messages == 2);
    const Json& msgs = c.chat["messages"];
    report("current branch followed (b, not a)", msgs.size() == 2 && msgs[1]["content"] == "```\nprint(1)\n```" && msgs[1]["modelName"] == "gpt-4o");
    report("roles and links", msgs[0]["role"] == "user" && msgs[1]["role"] == "assistant" && msgs[1]["parentId"] == msgs[0]["id"] &&
                                  c.chat["history"]["currentId"] == msgs[1]["id"] && c.chat["history"]["messages"][msgs[0]["id"].get<std::string>()]["childrenIds"][0] == msgs[1]["id"]);
    report("tag and model", c.chat["tags"][0] == "imported-chatgpt" && c.chat["models"][0] == "qwen3:8b" && msgs[1]["model"] == "qwen3:8b");
  }

  const Json claude = Json::parse(R"json([{
    "uuid": "x", "name": "Recipe ideas", "created_at": "2024-05-01T12:34:56.789Z",
    "chat_messages": [
      {"uuid": "m1", "sender": "human", "text": "Give me a recipe", "created_at": "2024-05-01T12:34:56Z"},
      {"uuid": "m2", "sender": "assistant", "text": "", "content": [{"type": "text", "text": "Here is one"}, {"type": "tool_use"}], "created_at": "2024-05-01T12:35:10Z"},
      {"uuid": "m3", "sender": "human", "text": "   ", "created_at": "2024-05-01T12:36:00Z"}
    ]}])json");
  report("detect claude", detect(claude) == Source::Claude);
  out = convert(claude, Source::Claude, "qwen3:8b");
  report("claude conversation", out.size() == 1 && out[0].title == "Recipe ideas" && out[0].messages == 2, out.empty() ? "" : std::to_string(out[0].messages));
  if (!out.empty()) {
    report("iso time parsed", out[0].createdAt == 1714566896, std::to_string(out[0].createdAt));
    report("content array text used, blank turn skipped", out[0].chat["messages"][1]["content"] == "Here is one" && out[0].chat["tags"][0] == "imported-claude");
  }
  report("unknown document", detect(Json::parse(R"({"a": 1})")) == Source::Unknown && detect(Json::array()) == Source::Unknown);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
