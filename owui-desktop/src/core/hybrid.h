// Hybrid local search (#102): SQLite FTS5 (BM25) beside the vector index,
// fused with reciprocal rank fusion so exact identifiers are found even when
// the embedding misses them. The FTS table shadows the `chunks` table through
// triggers, so the indexer needs no change. App-free; --hybridtest covers it.
#pragma once

#include <string>
#include <vector>

struct sqlite3;

namespace hybrid {

// Creates chunks_fts and its triggers next to the app's `chunks` table and
// backfills rows the FTS table does not have yet. False when FTS5 is missing.
bool ensureFts(sqlite3* db, std::string* error = nullptr);

struct Lexical {
  std::string path;
  int chunk = 0;
  std::string text;
  double bm25 = 0.0;   // lower is better (SQLite's sign convention)
};
std::vector<std::string> tokens(const std::string& query);   // identifiers kept whole (a_b, a.b), 2+ chars
std::string ftsQuery(const std::string& query);              // "tok"* OR "tok"* ... ; "" when nothing usable
std::vector<Lexical> lexicalSearch(sqlite3* db, const std::string& query, int k, std::string* error = nullptr);

struct Ranked {
  std::string path;
  int chunk = 0;
  std::string text;
};
struct Fused {
  std::string path;
  int chunk = 0;
  std::string text;
  double score = 0.0;
  int vectorRank = 0;    // 1-based, 0 = not in that list
  int lexicalRank = 0;
  bool exact = false;    // every query token appears verbatim in the text
};
// Reciprocal rank fusion of the two ranked lists (score = sum 1/(60+rank)),
// plus a bonus when the text contains every query token. `k` results.
std::vector<Fused> fuse(const std::vector<Ranked>& byVector, const std::vector<Ranked>& byText, const std::string& query, int k);
const char* sourceName(const Fused& hit);   // "both", "vector", "text"

}  // namespace hybrid
