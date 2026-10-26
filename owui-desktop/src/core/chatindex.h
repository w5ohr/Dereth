// Semantic chat search (#109): every message of every chat the app has seen
// is chunked, embedded locally (the caller supplies the embedding function -
// nomic-embed-text through Ollama in the app) and stored with its vector in
// one SQLite file, so the whole history can be searched by meaning, offline,
// without the server. Incremental: a chat is re-read only when its updated_at
// moved, and only chunks whose text changed are embedded again. App-free;
// --chatindextest drives it with a fake embedder.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chatindex {

using Embed = std::function<bool(const std::vector<std::string>& inputs, std::vector<std::vector<float>>* out,
                                 std::string* error)>;

struct Message {
  std::string id, role, content;
};

struct Hit {
  std::string chatId, messageId, title, role, text;
  float score = 0.0f;
  int64_t updatedAt = 0;
};

struct Stats {
  int chats = 0;
  int chunks = 0;
  int64_t bytes = 0;
  std::string path;
};

bool open(const std::string& path);
void close();
bool ready();

// True when the chat is unknown or was updated since it was indexed.
bool needsIndex(const std::string& chatId, int64_t updatedAt);
// Re-reads one chat: embeds new or changed chunks, drops the rest. Returns the
// number of chunks embedded, -1 on an embedding failure (nothing is dropped then).
int indexChat(const std::string& chatId, const std::string& title, int64_t updatedAt,
              const std::vector<Message>& messages, const Embed& embed, std::string* error);
void removeChat(const std::string& chatId);
// The chats the index knows, so callers can drop the ones that vanished.
std::vector<std::string> chatIds();

// Embeds the query and scans every vector; one hit per message, best first.
std::vector<Hit> search(const std::string& query, int k, const Embed& embed, std::string* error);

Stats stats();
void clear();

// ---- pieces, exposed for the self-test ------------------------------------
std::vector<std::string> chunk(const std::string& text, size_t maxChars);
std::string hashOf(const std::string& text);

}  // namespace chatindex
