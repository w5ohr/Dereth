// The local embedding call (nomic-embed-text through the Ollama URL) that the
// local file index uses, exposed for the chat index.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace app {

// Blocking. Unit-length vectors, one per input; false with `error` set when
// Ollama is unreachable or the model is missing.
bool embedTexts(const std::vector<std::string>& inputs, std::vector<std::vector<float>>* out, std::string* error);
std::string embedModelName();

// The local index's machinery for another store (the personal corpus, #106):
// same chunking, embeddings and hybrid search, in the database at dbPath.
std::vector<std::string> chunkForIndex(const std::string& text);
struct IndexHit {
  std::string path, text;
  float score = 0.0f;
};
std::vector<IndexHit> searchIndexFile(const std::string& dbPath, const std::string& query, int k, std::string* error);
std::map<std::string, int64_t> indexedPaths(const std::string& dbPath);
int indexedChunkCount(const std::string& dbPath);
bool removeIndexed(const std::string& dbPath, const std::string& path);
// Embeds and stores `chunks` as `path` (replacing what was there). Blocking.
bool indexDocument(const std::string& dbPath, const std::string& path, int64_t mtime, const std::string& embedPrefix,
                   const std::vector<std::string>& chunks, std::string* error);

}  // namespace app
