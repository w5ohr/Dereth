// Retrieval inspector (#104): what the server retrieved for a reply - every
// chunk with its source, page and relevance - plus pin / exclude marks and
// the re-ask prompt that sends only the chosen passages. App-free;
// --retrievaltest covers it.
#pragma once

#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace retrieval {

using Json = nlohmann::json;

struct Chunk {
  int source = 0;        // index into the reply's sources array
  int index = 0;         // index within that source's documents
  std::string id;        // stable: hash of name, page and text
  std::string name;      // file name, page title or URL
  std::string url;       // web sources
  std::string fileId;    // server file id when known
  std::string text;
  int page = 0;          // 1-based when known
  double relevance = -1; // 0..1 when the server sent one (higher is better), else -1
};

// Flattens Open WebUI's `sources` ([{source:{...}, document:[...], metadata:[...], distances:[...]}]).
std::vector<Chunk> fromSources(const Json& sources);
// Most relevant first; chunks without a score keep their order after the scored ones.
std::vector<Chunk> ranked(std::vector<Chunk> chunks);

struct Marks {
  std::set<std::string> pinned, excluded;
};
Marks marksFromJson(const Json& j);
Json marksToJson(const Marks& marks);
void togglePin(Marks& marks, const std::string& id);       // pinning clears an exclusion
void toggleExclude(Marks& marks, const std::string& id);   // excluding clears a pin

struct Stats {
  int chunks = 0, sources = 0, scored = 0, pinned = 0, excluded = 0;
  double mean = 0.0, best = 0.0;
};
Stats stats(const std::vector<Chunk>& chunks, const Marks& marks);

// The passages a re-ask sends: the pinned ones, or - with nothing pinned -
// every chunk that is not excluded. Empty when no mark was made.
std::vector<Chunk> passagesForReask(const std::vector<Chunk>& chunks, const Marks& marks);
// The question again with the chosen passages quoted, and the instruction
// to use only those.
std::string reaskPrompt(const std::string& question, const std::vector<Chunk>& passages);
std::string copyText(const std::vector<Chunk>& passages);   // plain list for the clipboard
std::string relevanceLabel(double relevance);               // "82%" or "n/a"

}  // namespace retrieval
