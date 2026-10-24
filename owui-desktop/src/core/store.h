// Local-first chat mirror: a SQLite copy of every chat the app has seen for
// the current server, with full-text search, so chats can be read and
// searched with the server down and edits made while offline are pushed
// when it returns.
//
// One database per server (supportDir()/mirror-<host>.db). Every call is
// safe from any thread; each takes the store's own mutex.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace store {

using Json = nlohmann::json;

struct ChatRow {
  std::string id;
  std::string title;
  std::string folderId;
  bool pinned = false;
  bool archived = false;
  int64_t updatedAt = 0;
  bool dirty = false;
};

struct Stats {
  int chats = 0;
  int dirty = 0;
  int64_t bytes = 0;
  int64_t lastSync = 0;      // unix seconds of the last full sync, 0 = never
  std::string path;
};

// Opens (creating if needed) the mirror for a server URL. Re-opening the same
// server is a no-op; a different server closes the previous database.
bool open(const std::string& serverUrl);
void close();
bool ready();

// Stores a chat as the server returned it (a ChatResponse: id, title, chat,
// updated_at, pinned, archived, folder_id). Clears the dirty flag.
void putChat(const Json& response);
// Updates list fields from a chat-list item without touching the body.
void putListItem(const Json& item);
// Remembers a chat body that could not be sent to the server.
// `baseUpdatedAt` is the server's updated_at the edit was made against.
void putDirty(const std::string& id, const Json& chatBody, int64_t baseUpdatedAt);
std::vector<std::pair<std::string, Json>> dirtyChats();   // id -> stored ChatResponse-shaped row
int64_t dirtyBase(const std::string& id);
void removeChat(const std::string& id);

std::optional<Json> getChat(const std::string& id);         // ChatResponse-shaped
std::vector<ChatRow> listChats(bool archived);
std::vector<ChatRow> search(const std::string& query, int limit = 50);
std::vector<std::string> chatIds();

void setMeta(const std::string& key, const std::string& value);
std::string meta(const std::string& key);
Stats stats();
void clear();   // drops every row (the next sync refills it)

}  // namespace store
