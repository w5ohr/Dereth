// `Open WebUI --librarytest`: offline checks of the saved answers library.
#include <cstdio>
#include <string>

#include "core/library.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
library::Entry make(const std::string& chat, const std::string& msg, const std::string& text, int64_t at, const std::string& tags = "", const std::string& note = "") {
  library::Entry e;
  e.chatId = chat;
  e.messageId = msg;
  e.chatTitle = "Chat " + chat;
  e.role = "assistant";
  e.model = "qwen3:8b";
  e.text = text;
  e.note = note;
  e.tags = library::parseTags(tags);
  e.at = at;
  return e;
}
}  // namespace

int runLibrarySelfTest() {
  using namespace library;
  printf("Open WebUI desktop saved-answers self-test\n\n");

  report("parseTags: separators, case, #, duplicates", parseTags("Recipes, #sql  code;recipes\nSQL") == std::vector<std::string>({"recipes", "sql", "code"}) && parseTags("").empty() && parseTags(" , # ").empty());

  std::vector<Entry> lib;
  const std::string a = add(lib, make("c1", "m1", "SELECT 1 is the simplest query.", 1700000100, "sql, code"));
  add(lib, make("c1", "m2", "Use a regex for dates.", 1700000200, "regex"));
  add(lib, make("c2", "m9", "Canberra is the capital.", 1700000300, "", "geography fact"));
  const std::string again = add(lib, make("c1", "m1", "SELECT 1 is the simplest query (edited).", 999, "sqlite"));
  report("add: ids assigned, the same message updates in place (text, tags merged; time kept)", lib.size() == 3 && again == a && lib[0].text.find("(edited)") != std::string::npos && lib[0].at == 1700000100 &&
                                                                                                   lib[0].tags == std::vector<std::string>({"sql", "code", "sqlite"}) && find(lib, "c1", "m2") && !find(lib, "c1", "zz") &&
                                                                                                   !find(lib, "c1", ""));
  std::vector<Entry> back = fromJson(toJson(lib));
  report("JSON round-trip; entries without text dropped; junk ignored", back.size() == 3 && back[2].note == "geography fact" && back[0].tags.size() == 3 && back[1].at == 1700000200 &&
                                                                            fromJson(Json::array({Json{{"id", "x"}}})).empty() && fromJson(Json("no")).empty());

  report("search: all words, any field, case-insensitive, newest first", search(lib, "").size() == 3 && search(lib, "")[0].chatId == "c2" && search(lib, "QUERY select").size() == 1 &&
                                                                             search(lib, "capital geography").size() == 1 && search(lib, "chat c1").size() == 2 && search(lib, "qwen3").size() == 3 &&
                                                                             search(lib, "nothing here").empty());
  report("search: #tag words match tags only (prefix ok)", search(lib, "#sql").size() == 1 && search(lib, "#sq").size() == 1 && search(lib, "#regex dates").size() == 1 && search(lib, "#canberra").empty());
  const auto tags = tagCounts(lib);
  report("tagCounts: most used first, ties alphabetical", tags.size() == 4 && tags[0].first == "code" && tags[0].second == 1 && tags[3].first == "sqlite");
  report("remove", remove(lib, lib[1].id) && lib.size() == 2 && !remove(lib, "nope"));
  report("snippet: whitespace collapsed, cut on a UTF-8 boundary", snippet("  a\n\n b\tc  ", 20) == "a b c" && snippet("abcdefghij", 5) == "abcde..." && snippet("ab\xC3\xA9", 3) == "ab...");
  const std::string md = markdown(lib);
  report("markdown export: heading per entry with date, meta line, note as quote, text",
         md.find("# Saved answers\n\n## Chat c1 - 2023-11-14\n\n_qwen3:8b - tags: sql, code, sqlite_\n\nSELECT 1") != std::string::npos && md.find("> geography fact\n\nCanberra") != std::string::npos,
         md.substr(0, 90));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
