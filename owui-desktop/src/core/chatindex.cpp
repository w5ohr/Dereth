#include "chatindex.h"

#include <sqlite3.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "common.h"

namespace chatindex {
namespace {

std::mutex gMutex;
sqlite3* gDb = nullptr;
std::string gPath;

struct Statement {
  sqlite3_stmt* stmt = nullptr;
  Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr); }
  ~Statement() { sqlite3_finalize(stmt); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  void text(int i, const std::string& s) { sqlite3_bind_text(stmt, i, s.c_str(), -1, SQLITE_TRANSIENT); }
  void i64(int i, int64_t v) { sqlite3_bind_int64(stmt, i, v); }
  void blob(int i, const std::vector<float>& v) {
    sqlite3_bind_blob(stmt, i, v.data(), static_cast<int>(v.size() * sizeof(float)), SQLITE_TRANSIENT);
  }
  bool step() { return sqlite3_step(stmt) == SQLITE_ROW; }
  std::string col(int i) {
    const unsigned char* t = sqlite3_column_text(stmt, i);
    return t ? reinterpret_cast<const char*>(t) : "";
  }
};

bool exec(const char* sql) { return gDb && sqlite3_exec(gDb, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }

// Vectors are stored unit-length, so cosine similarity is a dot product.
void normalize(std::vector<float>* v) {
  double n = 0.0;
  for (float x : *v) n += static_cast<double>(x) * x;
  n = std::sqrt(n);
  if (n > 0.0) {
    for (float& x : *v) x = static_cast<float>(x / n);
  }
}

}  // namespace

bool open(const std::string& path) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gDb && gPath == path) return true;
  if (gDb) {
    sqlite3_close(gDb);
    gDb = nullptr;
  }
  if (sqlite3_open(path.c_str(), &gDb) != SQLITE_OK) {
    sqlite3_close(gDb);
    gDb = nullptr;
    return false;
  }
  gPath = path;
  exec("PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;"
       "CREATE TABLE IF NOT EXISTS chats(id TEXT PRIMARY KEY, title TEXT NOT NULL, updated_at INTEGER NOT NULL);"
       "CREATE TABLE IF NOT EXISTS chunks(chat_id TEXT NOT NULL, message_id TEXT NOT NULL, n INTEGER NOT NULL,"
       " role TEXT NOT NULL, text TEXT NOT NULL, hash TEXT NOT NULL, vector BLOB NOT NULL,"
       " PRIMARY KEY(chat_id, message_id, n));"
       "CREATE INDEX IF NOT EXISTS chunks_chat ON chunks(chat_id);");
  return true;
}

void close() {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gDb) sqlite3_close(gDb);
  gDb = nullptr;
  gPath.clear();
}

bool ready() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gDb != nullptr;
}

std::vector<std::string> chunk(const std::string& text, size_t maxChars) {
  std::vector<std::string> out;
  std::string current;
  auto flush = [&] {
    const std::string t = core::trim(current);
    if (!t.empty()) out.push_back(t);
    current.clear();
  };
  for (const std::string& para : core::split(text, '\n')) {
    if (!current.empty() && current.size() + para.size() + 1 > maxChars) flush();
    if (para.size() > maxChars) {
      flush();
      for (size_t at = 0; at < para.size(); at += maxChars) {
        current = para.substr(at, maxChars);
        flush();
      }
      continue;
    }
    current += para + "\n";
  }
  flush();
  return out;
}

std::string hashOf(const std::string& text) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : text) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[24];
  snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

bool needsIndex(const std::string& chatId, int64_t updatedAt) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return false;
  Statement s(gDb, "SELECT updated_at FROM chats WHERE id = ?");
  s.text(1, chatId);
  if (!s.step()) return true;
  return sqlite3_column_int64(s.stmt, 0) < updatedAt;
}

int indexChat(const std::string& chatId, const std::string& title, int64_t updatedAt,
              const std::vector<Message>& messages, const Embed& embed, std::string* error) {
  constexpr size_t kChunkChars = 900;
  // What should be there.
  struct Want {
    std::string messageId, role, text, hash;
    int n;
  };
  std::vector<Want> wanted;
  for (const Message& m : messages) {
    if (m.role != "user" && m.role != "assistant") continue;
    if (core::trim(m.content).empty()) continue;
    int n = 0;
    for (const std::string& piece : chunk(m.content, kChunkChars)) {
      wanted.push_back({m.id, m.role, piece, hashOf(piece), n++});
    }
  }
  // What is there.
  std::map<std::string, std::string> existing;   // "message_id/n" -> hash
  {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gDb) {
      if (error) *error = "index not open";
      return -1;
    }
    Statement s(gDb, "SELECT message_id, n, hash FROM chunks WHERE chat_id = ?");
    s.text(1, chatId);
    while (s.step()) existing[s.col(0) + "/" + std::to_string(sqlite3_column_int(s.stmt, 1))] = s.col(2);
  }
  std::vector<size_t> toEmbed;
  std::set<std::string> keep;
  for (size_t i = 0; i < wanted.size(); ++i) {
    const std::string key = wanted[i].messageId + "/" + std::to_string(wanted[i].n);
    keep.insert(key);
    auto it = existing.find(key);
    if (it == existing.end() || it->second != wanted[i].hash) toEmbed.push_back(i);
  }
  std::vector<std::vector<float>> vectors;
  if (!toEmbed.empty()) {
    std::vector<std::string> inputs;
    for (size_t i : toEmbed) {
      // The title rides along: questions often name what a chat was about.
      inputs.push_back("search_document: " + title + "\n" + wanted[i].text);
    }
    // In batches of 16 so one failure is cheap and progress is steady.
    for (size_t at = 0; at < inputs.size(); at += 16) {
      std::vector<std::string> batch(inputs.begin() + static_cast<long>(at),
                                     inputs.begin() + static_cast<long>(std::min(inputs.size(), at + 16)));
      std::vector<std::vector<float>> part;
      if (!embed(batch, &part, error) || part.size() != batch.size()) {
        if (error && error->empty()) *error = "embedding failed";
        return -1;
      }
      for (auto& v : part) {
        normalize(&v);
        vectors.push_back(std::move(v));
      }
    }
  }
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return -1;
  exec("BEGIN");
  for (const auto& [key, hash] : existing) {
    if (keep.count(key)) continue;
    const size_t slash = key.rfind('/');
    Statement d(gDb, "DELETE FROM chunks WHERE chat_id = ? AND message_id = ? AND n = ?");
    d.text(1, chatId);
    d.text(2, key.substr(0, slash));
    d.i64(3, atoi(key.c_str() + slash + 1));
    d.step();
  }
  for (size_t k = 0; k < toEmbed.size(); ++k) {
    const Want& w = wanted[toEmbed[k]];
    Statement u(gDb, "INSERT INTO chunks(chat_id, message_id, n, role, text, hash, vector) VALUES(?, ?, ?, ?, ?, ?, ?)"
                     " ON CONFLICT(chat_id, message_id, n) DO UPDATE SET role=excluded.role, text=excluded.text,"
                     " hash=excluded.hash, vector=excluded.vector");
    u.text(1, chatId);
    u.text(2, w.messageId);
    u.i64(3, w.n);
    u.text(4, w.role);
    u.text(5, w.text);
    u.text(6, w.hash);
    u.blob(7, vectors[k]);
    u.step();
  }
  Statement c(gDb, "INSERT INTO chats(id, title, updated_at) VALUES(?, ?, ?)"
                   " ON CONFLICT(id) DO UPDATE SET title=excluded.title, updated_at=excluded.updated_at");
  c.text(1, chatId);
  c.text(2, title);
  c.i64(3, updatedAt);
  c.step();
  exec("COMMIT");
  return static_cast<int>(toEmbed.size());
}

void removeChat(const std::string& chatId) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return;
  Statement a(gDb, "DELETE FROM chunks WHERE chat_id = ?");
  a.text(1, chatId);
  a.step();
  Statement b(gDb, "DELETE FROM chats WHERE id = ?");
  b.text(1, chatId);
  b.step();
}

std::vector<std::string> chatIds() {
  std::vector<std::string> out;
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return out;
  Statement s(gDb, "SELECT id FROM chats");
  while (s.step()) out.push_back(s.col(0));
  return out;
}

std::vector<Hit> search(const std::string& query, int k, const Embed& embed, std::string* error) {
  std::vector<Hit> out;
  if (core::trim(query).empty() || k <= 0) return out;
  std::vector<std::vector<float>> qv;
  if (!embed({"search_query: " + query}, &qv, error) || qv.size() != 1) {
    if (error && error->empty()) *error = "embedding failed";
    return out;
  }
  normalize(&qv[0]);
  const std::vector<float>& q = qv[0];

  std::map<std::string, Hit> best;   // message id -> best chunk
  {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gDb) return out;
    Statement s(gDb, "SELECT c.chat_id, c.message_id, c.role, c.text, c.vector, h.title, h.updated_at"
                     " FROM chunks c JOIN chats h ON h.id = c.chat_id");
    while (s.step()) {
      const int bytes = sqlite3_column_bytes(s.stmt, 4);
      const float* v = static_cast<const float*>(sqlite3_column_blob(s.stmt, 4));
      const size_t n = static_cast<size_t>(bytes) / sizeof(float);
      if (!v || n != q.size()) continue;
      float dot = 0.0f;
      for (size_t i = 0; i < n; ++i) dot += v[i] * q[i];
      const std::string messageId = s.col(1);
      auto it = best.find(messageId);
      if (it != best.end() && it->second.score >= dot) continue;
      Hit hit;
      hit.chatId = s.col(0);
      hit.messageId = messageId;
      hit.role = s.col(2);
      hit.text = s.col(3);
      hit.score = dot;
      hit.title = s.col(5);
      hit.updatedAt = sqlite3_column_int64(s.stmt, 6);
      best[messageId] = hit;
    }
  }
  for (auto& [id, hit] : best) out.push_back(hit);
  std::sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
  if (static_cast<int>(out.size()) > k) out.resize(static_cast<size_t>(k));
  return out;
}

Stats stats() {
  Stats s;
  std::lock_guard<std::mutex> lock(gMutex);
  s.path = gPath;
  if (!gDb) return s;
  {
    Statement a(gDb, "SELECT COUNT(*) FROM chats");
    if (a.step()) s.chats = sqlite3_column_int(a.stmt, 0);
  }
  {
    Statement b(gDb, "SELECT COUNT(*) FROM chunks");
    if (b.step()) s.chunks = sqlite3_column_int(b.stmt, 0);
  }
  struct stat st {};
  if (stat(gPath.c_str(), &st) == 0) s.bytes = st.st_size;
  return s;
}

void clear() {
  std::lock_guard<std::mutex> lock(gMutex);
  exec("DELETE FROM chunks; DELETE FROM chats; VACUUM;");
}

}  // namespace chatindex
