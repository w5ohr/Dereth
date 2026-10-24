#include "core/store.h"

#include <sqlite3.h>
#include <sys/stat.h>

#include <mutex>

#include "core/common.h"

namespace store {
namespace {

std::mutex gMutex;
sqlite3* gDb = nullptr;
std::string gServer;
std::string gPath;

struct Statement {
  sqlite3_stmt* stmt = nullptr;
  Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr); }
  ~Statement() { sqlite3_finalize(stmt); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  explicit operator bool() const { return stmt != nullptr; }
  void text(int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }
  void integer(int index, int64_t value) { sqlite3_bind_int64(stmt, index, value); }
  bool step() { return sqlite3_step(stmt) == SQLITE_ROW; }
  std::string column(int col) const {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
  }
  int64_t integerColumn(int col) const { return sqlite3_column_int64(stmt, col); }
};

bool exec(const char* sql) { return gDb && sqlite3_exec(gDb, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }

std::string hostOf(const std::string& url) {
  std::string host = url;
  const size_t scheme = host.find("://");
  if (scheme != std::string::npos) host = host.substr(scheme + 3);
  const size_t slash = host.find('/');
  if (slash != std::string::npos) host = host.substr(0, slash);
  std::string safe;
  for (char ch : host) safe += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-') ? ch : '_';
  return safe.empty() ? "server" : safe;
}

// The searchable text of a chat: its title and every message body on record.
std::string bodyText(const Json& response) {
  std::string text = response.value("title", "");
  const Json chat = response.value("chat", Json::object());
  if (chat.is_object() && chat.contains("history") && chat["history"].is_object()) {
    const Json messages = chat["history"].value("messages", Json::object());
    if (messages.is_object()) {
      for (auto it = messages.begin(); it != messages.end(); ++it) {
        if (!it.value().is_object()) continue;
        const Json& content = it.value().contains("content") ? it.value()["content"] : Json();
        if (content.is_string()) text += "\n" + content.get<std::string>();
      }
    }
  }
  return text;
}

void indexChat(const std::string& id, const std::string& title, const std::string& body) {
  {
    Statement del(gDb, "DELETE FROM chats_fts WHERE id = ?");
    del.text(1, id);
    del.step();
  }
  Statement ins(gDb, "INSERT INTO chats_fts(id, title, body) VALUES(?, ?, ?)");
  ins.text(1, id);
  ins.text(2, title);
  ins.text(3, body);
  ins.step();
}

ChatRow rowFrom(const Statement& s) {
  ChatRow row;
  row.id = s.column(0);
  row.title = s.column(1);
  row.folderId = s.column(2);
  row.pinned = s.integerColumn(3) != 0;
  row.archived = s.integerColumn(4) != 0;
  row.updatedAt = s.integerColumn(5);
  row.dirty = s.integerColumn(6) != 0;
  return row;
}

// FTS5 treats punctuation as syntax; every term is quoted and prefix-matched.
std::string ftsQuery(const std::string& query) {
  std::string out;
  for (const std::string& term : core::split(core::trim(query), ' ')) {
    if (term.empty()) continue;
    if (!out.empty()) out += " ";
    out += "\"" + core::replaceAll(term, "\"", "\"\"") + "\"*";
  }
  return out;
}

}  // namespace

bool open(const std::string& serverUrl) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gDb && gServer == serverUrl) return true;
  if (gDb) {
    sqlite3_close(gDb);
    gDb = nullptr;
  }
  gServer = serverUrl;
  gPath = core::supportDir() + "/mirror-" + hostOf(serverUrl) + ".db";
  if (sqlite3_open(gPath.c_str(), &gDb) != SQLITE_OK) {
    sqlite3_close(gDb);
    gDb = nullptr;
    return false;
  }
  const bool ok =
      exec("PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;"
           "CREATE TABLE IF NOT EXISTS chats(id TEXT PRIMARY KEY, title TEXT, folder_id TEXT,"
           " pinned INTEGER DEFAULT 0, archived INTEGER DEFAULT 0, updated_at INTEGER DEFAULT 0,"
           " json TEXT, dirty INTEGER DEFAULT 0, base_updated INTEGER DEFAULT 0);"
           "CREATE VIRTUAL TABLE IF NOT EXISTS chats_fts USING fts5(id UNINDEXED, title, body);"
           "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);");
  if (!ok) {
    sqlite3_close(gDb);
    gDb = nullptr;
  }
  core::debugLog("mirror %s: %s", gPath.c_str(), ok ? "open" : "failed to open");
  return ok;
}

void close() {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gDb) sqlite3_close(gDb);
  gDb = nullptr;
  gServer.clear();
}

bool ready() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gDb != nullptr;
}

void putChat(const Json& response) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb || !response.is_object()) return;
  const std::string id = response.value("id", "");
  if (id.empty()) return;
  const std::string title = response.value("title", "");
  Statement s(gDb,
              "INSERT INTO chats(id, title, folder_id, pinned, archived, updated_at, json, dirty, base_updated)"
              " VALUES(?, ?, ?, ?, ?, ?, ?, 0, ?)"
              " ON CONFLICT(id) DO UPDATE SET title=excluded.title, folder_id=excluded.folder_id,"
              " pinned=excluded.pinned, archived=excluded.archived, updated_at=excluded.updated_at,"
              " json=excluded.json, dirty=0, base_updated=excluded.base_updated");
  s.text(1, id);
  s.text(2, title);
  s.text(3, response.contains("folder_id") && response["folder_id"].is_string() ? response["folder_id"].get<std::string>() : "");
  s.integer(4, response.value("pinned", false) ? 1 : 0);
  s.integer(5, response.value("archived", false) ? 1 : 0);
  const int64_t updated = response.value("updated_at", static_cast<int64_t>(0));
  s.integer(6, updated);
  s.text(7, response.dump());
  s.integer(8, updated);
  s.step();
  indexChat(id, title, bodyText(response));
}

void putListItem(const Json& item) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb || !item.is_object()) return;
  const std::string id = item.value("id", "");
  if (id.empty()) return;
  Statement s(gDb,
              "UPDATE chats SET title = ?, folder_id = ?, pinned = ?, archived = ?,"
              " updated_at = MAX(updated_at, ?) WHERE id = ? AND dirty = 0");
  s.text(1, item.value("title", ""));
  s.text(2, item.contains("folder_id") && item["folder_id"].is_string() ? item["folder_id"].get<std::string>() : "");
  s.integer(3, item.value("pinned", false) ? 1 : 0);
  s.integer(4, item.value("archived", false) ? 1 : 0);
  s.integer(5, item.value("updated_at", static_cast<int64_t>(0)));
  s.text(6, id);
  s.step();
}

void putDirty(const std::string& id, const Json& chatBody, int64_t baseUpdatedAt) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb || id.empty()) return;
  Json response{{"id", id},
                {"title", chatBody.value("title", "New Chat")},
                {"chat", chatBody},
                {"updated_at", core::nowSeconds()},
                {"pinned", false},
                {"archived", false}};
  // Keep list flags the mirror already knows.
  {
    Statement q(gDb, "SELECT pinned, archived, folder_id FROM chats WHERE id = ?");
    q.text(1, id);
    if (q.step()) {
      response["pinned"] = q.integerColumn(0) != 0;
      response["archived"] = q.integerColumn(1) != 0;
      if (!q.column(2).empty()) response["folder_id"] = q.column(2);
    }
  }
  Statement s(gDb,
              "INSERT INTO chats(id, title, folder_id, pinned, archived, updated_at, json, dirty, base_updated)"
              " VALUES(?, ?, ?, ?, ?, ?, ?, 1, ?)"
              " ON CONFLICT(id) DO UPDATE SET title=excluded.title, updated_at=excluded.updated_at,"
              " json=excluded.json, dirty=1, base_updated=CASE WHEN chats.dirty=1 THEN chats.base_updated ELSE excluded.base_updated END");
  s.text(1, id);
  s.text(2, response["title"]);
  s.text(3, response.value("folder_id", ""));
  s.integer(4, response["pinned"].get<bool>() ? 1 : 0);
  s.integer(5, response["archived"].get<bool>() ? 1 : 0);
  s.integer(6, response["updated_at"]);
  s.text(7, response.dump());
  s.integer(8, baseUpdatedAt);
  s.step();
  indexChat(id, response["title"], bodyText(response));
}

std::vector<std::pair<std::string, Json>> dirtyChats() {
  std::lock_guard<std::mutex> lock(gMutex);
  std::vector<std::pair<std::string, Json>> out;
  if (!gDb) return out;
  Statement s(gDb, "SELECT id, json FROM chats WHERE dirty = 1");
  while (s.step()) {
    Json j = Json::parse(s.column(1), nullptr, false);
    if (!j.is_discarded()) out.emplace_back(s.column(0), j);
  }
  return out;
}

int64_t dirtyBase(const std::string& id) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return 0;
  Statement s(gDb, "SELECT base_updated FROM chats WHERE id = ?");
  s.text(1, id);
  return s.step() ? s.integerColumn(0) : 0;
}

void removeChat(const std::string& id) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return;
  Statement s(gDb, "DELETE FROM chats WHERE id = ?");
  s.text(1, id);
  s.step();
  Statement f(gDb, "DELETE FROM chats_fts WHERE id = ?");
  f.text(1, id);
  f.step();
}

std::optional<Json> getChat(const std::string& id) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return std::nullopt;
  Statement s(gDb, "SELECT json FROM chats WHERE id = ?");
  s.text(1, id);
  if (!s.step()) return std::nullopt;
  Json j = Json::parse(s.column(0), nullptr, false);
  if (j.is_discarded()) return std::nullopt;
  return j;
}

std::vector<ChatRow> listChats(bool archived) {
  std::lock_guard<std::mutex> lock(gMutex);
  std::vector<ChatRow> out;
  if (!gDb) return out;
  Statement s(gDb,
              "SELECT id, title, folder_id, pinned, archived, updated_at, dirty FROM chats"
              " WHERE archived = ? ORDER BY updated_at DESC");
  s.integer(1, archived ? 1 : 0);
  while (s.step()) out.push_back(rowFrom(s));
  return out;
}

std::vector<ChatRow> search(const std::string& query, int limit) {
  std::lock_guard<std::mutex> lock(gMutex);
  std::vector<ChatRow> out;
  const std::string match = ftsQuery(query);
  if (!gDb || match.empty()) return out;
  Statement s(gDb,
              "SELECT c.id, c.title, c.folder_id, c.pinned, c.archived, c.updated_at, c.dirty"
              " FROM chats_fts f JOIN chats c ON c.id = f.id WHERE chats_fts MATCH ?"
              " ORDER BY bm25(chats_fts) LIMIT ?");
  s.text(1, match);
  s.integer(2, limit);
  while (s.step()) out.push_back(rowFrom(s));
  return out;
}

std::vector<std::string> chatIds() {
  std::lock_guard<std::mutex> lock(gMutex);
  std::vector<std::string> out;
  if (!gDb) return out;
  Statement s(gDb, "SELECT id FROM chats");
  while (s.step()) out.push_back(s.column(0));
  return out;
}

void setMeta(const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return;
  Statement s(gDb, "INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  s.text(1, key);
  s.text(2, value);
  s.step();
}

std::string meta(const std::string& key) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gDb) return "";
  Statement s(gDb, "SELECT value FROM meta WHERE key = ?");
  s.text(1, key);
  return s.step() ? s.column(0) : "";
}

Stats stats() {
  std::lock_guard<std::mutex> lock(gMutex);
  Stats out;
  out.path = gPath;
  if (!gDb) return out;
  {
    Statement s(gDb, "SELECT COUNT(*), SUM(dirty) FROM chats");
    if (s.step()) {
      out.chats = static_cast<int>(s.integerColumn(0));
      out.dirty = static_cast<int>(s.integerColumn(1));
    }
  }
  {
    Statement s(gDb, "SELECT value FROM meta WHERE key = 'lastSync'");
    if (s.step()) out.lastSync = std::atoll(s.column(0).c_str());
  }
  struct stat st{};
  if (stat(gPath.c_str(), &st) == 0) out.bytes = st.st_size;
  return out;
}

void clear() {
  std::lock_guard<std::mutex> lock(gMutex);
  exec("DELETE FROM chats; DELETE FROM chats_fts; DELETE FROM meta;");
}

}  // namespace store
