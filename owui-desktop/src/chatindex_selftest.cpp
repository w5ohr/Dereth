// `Open WebUI --chatindextest`: the semantic chat index with a fake,
// deterministic embedder (bag of words hashed into 64 dimensions).
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "core/chatindex.h"
#include "core/common.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

int gEmbedCalls = 0, gEmbedInputs = 0;
bool fakeEmbed(const std::vector<std::string>& inputs, std::vector<std::vector<float>>* out, std::string* error) {
  (void)error;
  ++gEmbedCalls;
  gEmbedInputs += static_cast<int>(inputs.size());
  out->clear();
  for (const std::string& text : inputs) {
    std::vector<float> v(64, 0.0f);
    std::string word;
    auto push = [&] {
      if (word.size() >= 3) {
        size_t h = 1469598103934665603ULL;
        for (unsigned char c : word) h = (h ^ c) * 1099511628211ULL;
        v[h % 64] += 1.0f;
      }
      word.clear();
    };
    for (unsigned char c : text) {
      if (isalnum(c)) word += static_cast<char>(tolower(c));
      else push();
    }
    push();
    out->push_back(v);
  }
  return true;
}
bool failingEmbed(const std::vector<std::string>&, std::vector<std::vector<float>>*, std::string* error) {
  *error = "ollama down";
  return false;
}
}  // namespace

int runChatIndexSelfTest() {
  using namespace chatindex;
  printf("Open WebUI desktop chat-index self-test\n\n");

  const std::vector<std::string> pieces = chunk("alpha\nbeta\n" + std::string(2000, 'x') + "\ngamma", 900);
  report("chunk: paragraphs packed, a long line cut", pieces.size() == 5 && pieces[0] == "alpha\nbeta" && pieces[1].size() == 900 && pieces.back() == "gamma",
         std::to_string(pieces.size()));
  report("hashOf is stable and differs", hashOf("a") == hashOf("a") && hashOf("a") != hashOf("b"));

  const std::string path = "/tmp/owui-chatindextest-" + std::to_string(getpid()) + ".db";
  report("open", open(path) && ready());
  std::string error;
  std::vector<Message> sailing = {{"m1", "user", "How do I trim a jib sail when sailing upwind in strong wind?"},
                                  {"m2", "assistant", "Sheet the jib in until the telltales stream evenly; ease it in gusts."},
                                  {"m3", "system", "ignored"}};
  std::vector<Message> baking = {{"b1", "user", "My sourdough loaf is dense - what hydration should the dough have?"},
                                 {"b2", "assistant", "Try 75 percent hydration and a longer bulk ferment for an open crumb."}};
  report("needsIndex: unknown chat", needsIndex("sail", 100));
  const int n1 = indexChat("sail", "Sailing upwind", 100, sailing, fakeEmbed, &error);
  const int n2 = indexChat("bake", "Sourdough", 200, baking, fakeEmbed, &error);
  report("indexChat: user and assistant chunks embedded, system skipped", n1 == 2 && n2 == 2 && error.empty(), error);
  report("needsIndex: false once indexed, true after an update", !needsIndex("sail", 100) && needsIndex("sail", 101));

  gEmbedCalls = 0;
  gEmbedInputs = 0;
  sailing[1].content = "Sheet the jib in until the telltales stream evenly; ease it in gusts. Reef early.";
  sailing.push_back({"m4", "user", "Thanks!"});
  const int n3 = indexChat("sail", "Sailing upwind", 101, sailing, fakeEmbed, &error);
  report("re-index embeds only the changed and new chunks", n3 == 2 && gEmbedInputs == 2, std::to_string(n3) + " / " + std::to_string(gEmbedInputs));
  sailing.erase(sailing.begin() + 3);
  indexChat("sail", "Sailing upwind", 102, sailing, fakeEmbed, &error);
  Stats s = stats();
  report("removed message's chunk dropped; stats", s.chats == 2 && s.chunks == 4, std::to_string(s.chats) + " chats, " + std::to_string(s.chunks) + " chunks");

  std::vector<Hit> hits = search("sourdough dough hydration", 5, fakeEmbed, &error);
  report("search: the baking chat ranks first, one hit per message, title and role carried",
         hits.size() >= 2 && hits[0].chatId == "bake" && hits[0].title == "Sourdough" && !hits[0].role.empty() &&
             hits[0].score > hits.back().score, hits.empty() ? "no hits" : hits[0].messageId);
  hits = search("trim the jib telltales", 1, fakeEmbed, &error);
  report("search: k limits, sailing chat for a sailing question", hits.size() == 1 && hits[0].chatId == "sail");
  std::vector<Hit> none = search("anything", 3, failingEmbed, &error);
  report("search: embedding failure is an error, no hits", none.empty() && error == "ollama down");
  error.clear();
  report("indexChat: embedding failure returns -1 and keeps the rows", indexChat("new", "New", 1, baking, failingEmbed, &error) == -1 && stats().chunks == 4);

  removeChat("bake");
  report("removeChat + chatIds", stats().chats == 1 && chatIds() == std::vector<std::string>{"sail"});
  clear();
  report("clear", stats().chunks == 0 && stats().chats == 0);
  close();
  unlink(path.c_str());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
