// `Open WebUI --retrievaltest`: offline checks of the retrieval inspector.
#include <cstdio>
#include <string>

#include "core/retrieval.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runRetrievalSelfTest() {
  using namespace retrieval;
  printf("Open WebUI desktop retrieval-inspector self-test\n\n");

  const Json sources = Json::array({
      Json{{"source", Json{{"name", "handbook.pdf"}, {"type", "file"}, {"id", "f1"}}},
           {"document", Json::array({"Leave requests go to the manager.", "  Holidays are 25 days.  ", 42})},
           {"metadata", Json::array({Json{{"page", 3}, {"file_id", "f1"}}, Json{{"page", 7}}})},
           {"distances", Json::array({0.41, 0.93})}},
      Json{{"source", Json{{"name", "Policy page"}, {"url", "https://example.org/policy"}}},
           {"document", Json::array({"Remote work needs approval."})},
           {"metadata", Json::array({Json{{"source", "https://example.org/policy"}}})}},
      "https://example.com/plain",
      Json{{"source", Json{{"type", "file"}, {"id", "f9"}}}, {"document", Json::array({"Unnamed file text."})}, {"metadata", Json::array({Json{{"name", "notes.txt"}}})}},
  });
  std::vector<Chunk> chunks = fromSources(sources);
  report("fromSources: one chunk per document string, names, pages 1-based, file ids, urls, relevance clamped, unscored = -1",
         chunks.size() == 4 && chunks[0].name == "handbook.pdf" && chunks[0].page == 4 && chunks[0].fileId == "f1" && chunks[0].relevance == 0.41 && chunks[1].page == 8 &&
             chunks[1].text == "Holidays are 25 days." && chunks[1].relevance == 0.93 && chunks[2].name == "Policy page" && chunks[2].url == "https://example.org/policy" &&
             chunks[2].relevance == -1 && chunks[3].name == "notes.txt" && chunks[3].fileId == "f9" && chunks[0].source == 0 && chunks[2].source == 1 && chunks[1].index == 1,
         std::to_string(chunks.size()));
  report("fromSources: ids are stable and distinct", chunks[0].id == fromSources(sources)[0].id && chunks[0].id != chunks[1].id && !chunks[0].id.empty() && fromSources(Json()).empty());

  std::vector<Chunk> r = ranked(chunks);
  report("ranked: best score first, unscored after in their order", r[0].relevance == 0.93 && r[1].relevance == 0.41 && r[2].name == "Policy page" && r[3].name == "notes.txt");

  Marks m;
  togglePin(m, chunks[0].id);
  toggleExclude(m, chunks[1].id);
  toggleExclude(m, chunks[0].id);   // excluding a pinned chunk unpins it
  report("marks: pin and exclude are exclusive and toggle", !m.pinned.count(chunks[0].id) && m.excluded.count(chunks[0].id) && m.excluded.count(chunks[1].id) &&
                                                               [&] { toggleExclude(m, chunks[1].id); togglePin(m, chunks[0].id); return !m.excluded.count(chunks[1].id) && m.pinned.count(chunks[0].id) && !m.excluded.count(chunks[0].id); }());
  Marks back = marksFromJson(marksToJson(m));
  report("marks round-trip through JSON; garbage is empty", back.pinned == m.pinned && back.excluded == m.excluded && marksFromJson(Json{{"pinned", "x"}}).pinned.empty() && marksFromJson(Json()).excluded.empty());

  const Stats s = stats(chunks, m);
  report("stats: chunks, sources, scored, mean and best, marks", s.chunks == 4 && s.sources == 3 && s.scored == 2 && s.best == 0.93 && s.mean > 0.669 && s.mean < 0.671 && s.pinned == 1 && s.excluded == 0,
         "sources " + std::to_string(s.sources) + " mean " + std::to_string(s.mean));

  report("passagesForReask: pinned wins; else everything not excluded; nothing marked = nothing",
         passagesForReask(chunks, m).size() == 1 && passagesForReask(chunks, m)[0].id == chunks[0].id &&
             [&] { Marks e; toggleExclude(e, chunks[2].id); const auto p = passagesForReask(chunks, e); return p.size() == 3 && p[2].name == "notes.txt"; }() && passagesForReask(chunks, Marks{}).empty());

  const std::string prompt = reaskPrompt("  How many holidays?  ", {chunks[1], chunks[2]});
  report("reaskPrompt: instruction, numbered quoted passages with page, then the trimmed question",
         prompt.find("using only the passages") != std::string::npos && prompt.find("[1] handbook.pdf, page 8:\n\"\"\"\nHolidays are 25 days.\n\"\"\"") != std::string::npos &&
             prompt.find("[2] Policy page:\n") != std::string::npos && prompt.rfind("Question: How many holidays?") == prompt.size() - std::string("Question: How many holidays?").size(),
         prompt.substr(0, 80));
  report("copyText and relevanceLabel", copyText({chunks[1]}) == "1. handbook.pdf (p. 8) - relevance 93%\nHolidays are 25 days." && relevanceLabel(-1) == "n/a" && relevanceLabel(0.415) == "42%" &&
                                            copyText({}).empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
