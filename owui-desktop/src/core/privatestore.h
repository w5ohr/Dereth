// Private chats: conversations that never reach the server. Each one is a
// JSON document sealed with AES-256-CBC + HMAC-SHA256 (encrypt-then-MAC,
// CommonCrypto) under a 32-byte key the app keeps in the macOS keychain, and
// written to its own file in the private folder. App-free; --privatetest
// drives it offline.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace privatestore {

using Json = nlohmann::json;

// Sealed blob: "OWP1" + 16-byte IV + ciphertext + 32-byte tag. `key` is 32 bytes.
std::string seal(const std::string& plaintext, const std::string& key);
bool open(const std::string& blob, const std::string& key, std::string* plaintext);
std::string randomBytes(size_t count);

struct Entry {
  std::string id;
  std::string title;
  int64_t updatedAt = 0;
};

bool save(const std::string& dir, const std::string& key, const std::string& id, const Json& doc);
std::optional<Json> load(const std::string& dir, const std::string& key, const std::string& id);
bool remove(const std::string& dir, const std::string& id);
// Every chat in the folder with its title and time (decrypts each one).
std::vector<Entry> list(const std::string& dir, const std::string& key);

bool isPrivateId(const std::string& id);   // "local-..."
std::string newId();

}  // namespace privatestore
