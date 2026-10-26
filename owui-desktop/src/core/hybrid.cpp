#include "hybrid.h"

#include <sqlite3.h>

#include <algorithm>
#include <map>

#include "common.h"

namespace hybrid {
namespace {

constexpr double kRrf = 60.0;

bool exec(sqlite3* db, const char* sql, std::string* error) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) == SQLITE_OK) return true;
  if (error) *error = err ? err : "sqlite error";
  sqlite3_free(err);
  return false;
}

}  // namespace

bool ensureFts(sqlite3* db, std::string* error) {
  if (!db) return false;
  if (!exec(db,
            "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(path UNINDEXED, chunk UNINDEXED, text, tokenize=\"unicode61 tokenchars '_.'\");",
            error)) {
    return false;
  }
  const char* triggers =
      "CREATE TRIGGER IF NOT EXISTS chunks_fts_ai AFTER INSERT ON chunks BEGIN"
      "  INSERT INTO chunks_fts(rowid, path, chunk, text) VALUES (new.rowid, new.path, new.chunk, new.text); END;"
      "CREATE TRIGGER IF NOT EXISTS chunks_fts_ad AFTER DELETE ON chunks BEGIN"
      "  DELETE FROM chunks_fts WHERE rowid = old.rowid; END;"
      "CREATE TRIGGER IF NOT EXISTS chunks_fts_au AFTER UPDATE ON chunks BEGIN"
      "  DELETE FROM chunks_fts WHERE rowid = old.rowid;"
      "  INSERT INTO chunks_fts(rowid, path, chunk, text) VALUES (new.rowid, new.path, new.chunk, new.text); END;";
  if (!exec(db, triggers, error)) return false;
  // Rows indexed before the FTS table existed.
  return exec(db, "INSERT INTO chunks_fts(rowid, path, chunk, text) SELECT rowid, path, chunk, text FROM chunks WHERE rowid NOT IN (SELECT rowid FROM chunks_fts);", error);
}

std::vector<std::string> tokens(const std::string& query) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    while (!cur.empty() && (cur.back() == '.' || cur.back() == '_')) cur.pop_back();
    while (!cur.empty() && (cur.front() == '.' || cur.front() == '_')) cur.erase(0, 1);
    if (cur.size() >= 2 && std::find(out.begin(), out.end(), cur) == out.end()) out.push_back(cur);
    cur.clear();
  };
  for (unsigned char c : query) {
    if (isalnum(c) || c == '_' || c == '.' || c >= 0x80) cur += static_cast<char>(c);
    else flush();
  }
  flush();
  return out;
}

std::string ftsQuery(const std::string& query) {
  std::string out;
  for (const std::string& t : tokens(query)) {
    if (!out.empty()) out += " OR ";
    out += "\"" + core::replaceAll(t, "\"", "\"\"") + "\"*";
  }
  return out;
}

std::vector<Lexical> lexicalSearch(sqlite3* db, const std::string& query, int k, std::string* error) {
  std::vector<Lexical> out;
  const std::string match = ftsQuery(query);
  if (!db || match.empty() || k <= 0) return out;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT path, chunk, text, bm25(chunks_fts) FROM chunks_fts WHERE chunks_fts MATCH ? ORDER BY bm25(chunks_fts) LIMIT ?", -1, &st, nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db);
    return out;
  }
  sqlite3_bind_text(st, 1, match.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, k);
  while (sqlite3_step(st) == SQLITE_ROW) {
    Lexical l;
    const unsigned char* p = sqlite3_column_text(st, 0);
    const unsigned char* t = sqlite3_column_text(st, 2);
    l.path = p ? reinterpret_cast<const char*>(p) : "";
    l.chunk = sqlite3_column_int(st, 1);
    l.text = t ? reinterpret_cast<const char*>(t) : "";
    l.bm25 = sqlite3_column_double(st, 3);
    out.push_back(l);
  }
  sqlite3_finalize(st);
  return out;
}

std::vector<Fused> fuse(const std::vector<Ranked>& byVector, const std::vector<Ranked>& byText, const std::string& query, int k) {
  std::map<std::pair<std::string, int>, Fused> acc;
  auto key = [](const Ranked& r) { return std::make_pair(r.path, r.chunk); };
  for (size_t i = 0; i < byVector.size(); ++i) {
    Fused& f = acc[key(byVector[i])];
    f.path = byVector[i].path;
    f.chunk = byVector[i].chunk;
    f.text = byVector[i].text;
    f.vectorRank = static_cast<int>(i + 1);
    f.score += 1.0 / (kRrf + static_cast<double>(i + 1));
  }
  for (size_t i = 0; i < byText.size(); ++i) {
    Fused& f = acc[key(byText[i])];
    f.path = byText[i].path;
    f.chunk = byText[i].chunk;
    if (f.text.empty()) f.text = byText[i].text;
    f.lexicalRank = static_cast<int>(i + 1);
    f.score += 1.0 / (kRrf + static_cast<double>(i + 1));
  }
  const std::vector<std::string> toks = tokens(query);
  std::vector<Fused> out;
  for (auto& [id, f] : acc) {
    bool all = !toks.empty();
    const std::string lower = core::lower(f.text);
    for (const std::string& t : toks) all = all && lower.find(core::lower(t)) != std::string::npos;
    f.exact = all;
    if (all) f.score += 1.0 / kRrf;   // worth one first-place vote
    out.push_back(f);
  }
  std::stable_sort(out.begin(), out.end(), [](const Fused& a, const Fused& b) { return a.score > b.score; });
  if (k >= 0 && out.size() > static_cast<size_t>(k)) out.resize(static_cast<size_t>(k));
  return out;
}

const char* sourceName(const Fused& hit) {
  if (hit.vectorRank && hit.lexicalRank) return "both";
  return hit.vectorRank ? "vector" : "text";
}

}  // namespace hybrid
