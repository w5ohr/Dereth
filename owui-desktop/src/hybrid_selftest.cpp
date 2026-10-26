// `Open WebUI --hybridtest`: offline checks of hybrid local search.
#include <sqlite3.h>

#include <cstdio>
#include <string>

#include "core/hybrid.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
void insert(sqlite3* db, const char* path, int chunk, const char* text) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db, "INSERT INTO chunks(path, chunk, text, mtime, vector) VALUES(?,?,?,0,NULL)", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, chunk);
  sqlite3_bind_text(st, 3, text, -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}
}  // namespace

int runHybridSelfTest() {
  using namespace hybrid;
  printf("Open WebUI desktop hybrid-search self-test\n\n");

  report("tokens: identifiers kept whole, punctuation split, short and duplicate dropped", tokens("call fetch_user.profile(id) or fetchUserProfile, a fetch_user") == std::vector<std::string>({"call", "fetch_user.profile", "id", "or", "fetchUserProfile", "fetch_user"}) &&
                                                                                              tokens("  ").empty() && tokens("._xy_.") == std::vector<std::string>({"xy"}),
         [] { std::string j; for (const std::string& t : tokens("call fetch_user.profile(id) or fetchUserProfile, a fetch_user")) j += t + "|"; return j; }());
  report("ftsQuery: quoted prefix terms joined with OR", ftsQuery("fetch_user \"quoted\"") == "\"fetch_user\"* OR \"quoted\"*" && ftsQuery("!!").empty());

  sqlite3* db = nullptr;
  sqlite3_open(":memory:", &db);
  sqlite3_exec(db, "CREATE TABLE chunks(path TEXT NOT NULL, chunk INTEGER NOT NULL, text TEXT NOT NULL, mtime INTEGER NOT NULL, vector BLOB, PRIMARY KEY(path, chunk));", nullptr, nullptr, nullptr);
  insert(db, "old.py", 0, "def recalibrate_foghorn(): pass  # indexed before FTS existed");
  std::string error;
  const bool fts = ensureFts(db, &error);
  report("ensureFts: creates the FTS table and triggers, backfills older rows", fts && lexicalSearch(db, "recalibrate_foghorn", 5).size() == 1, error);
  insert(db, "users.py", 0, "def fetchUserProfile(user_id):\n    return db.get(user_id)");
  insert(db, "users.py", 1, "class UserProfile:\n    name = ''\n    def fetch(self): ...");
  insert(db, "readme.md", 0, "The profile service loads a user's data lazily.");
  std::vector<Lexical> hits = lexicalSearch(db, "fetchUserProfile", 5);
  report("lexicalSearch: the exact identifier ranks first through the insert trigger", hits.size() == 1 && hits[0].path == "users.py" && hits[0].chunk == 0 && hits[0].bm25 < 0, std::to_string(hits.size()));
  hits = lexicalSearch(db, "user profile", 5);
  report("lexicalSearch: prefix terms OR-ed, several hits, BM25 order (best first)", hits.size() >= 3 && hits[0].bm25 <= hits[1].bm25, std::to_string(hits.size()));
  sqlite3_exec(db, "DELETE FROM chunks WHERE path='old.py'", nullptr, nullptr, nullptr);
  sqlite3_exec(db, "UPDATE chunks SET text='nothing here' WHERE path='readme.md'", nullptr, nullptr, nullptr);
  report("triggers: delete and update keep the FTS table in step", lexicalSearch(db, "recalibrate_foghorn", 5).empty() && lexicalSearch(db, "lazily", 5).empty() && lexicalSearch(db, "nothing", 5).size() == 1);
  report("lexicalSearch: no usable tokens or no db is empty, not an error", lexicalSearch(db, "!!", 5).empty() && lexicalSearch(nullptr, "x", 5).empty());
  sqlite3_close(db);

  const std::vector<Ranked> byVector{{"a.py", 0, "alpha text"}, {"b.py", 0, "beta fetchUserProfile here"}, {"c.py", 0, "gamma"}};
  const std::vector<Ranked> byText{{"b.py", 0, "beta fetchUserProfile here"}, {"d.py", 0, "delta fetchuserprofile"}};
  std::vector<Fused> fused = fuse(byVector, byText, "fetchUserProfile", 10);
  report("fuse: a chunk on both lists wins, exact-token bonus lifts text-only hits above vector-only ones, sources named",
         fused.size() == 4 && fused[0].path == "b.py" && std::string(sourceName(fused[0])) == "both" && fused[0].exact && fused[1].path == "d.py" && std::string(sourceName(fused[1])) == "text" &&
             fused[1].exact && fused[2].path == "a.py" && std::string(sourceName(fused[2])) == "vector" && !fused[2].exact && fused[0].vectorRank == 2 && fused[0].lexicalRank == 1,
         fused.empty() ? "none" : fused[0].path + " / " + fused[1].path + " / " + fused[2].path);
  report("fuse: k cuts, empty lists ok, text from the vector list wins when both have it", fuse(byVector, byText, "x", 2).size() == 2 && fuse({}, {}, "x", 3).empty() && fuse(byVector, {}, "", 5)[0].path == "a.py" && !fuse(byVector, {}, "", 5)[0].exact);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
