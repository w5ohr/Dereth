// Model library browser (#73): search the Ollama library (its search and
// tags pages, scraped) and Hugging Face GGUF repositories (the Hub API),
// list the variants with their sizes, name what Ollama should pull
// ("qwen3:8b" or "hf.co/<repo>:<QUANT>"), check the disk, and read pull
// progress lines. This is the pure part; the app fetches and pulls.
// App-free; --modellibtest covers the parsers.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace modellib {

using Json = nlohmann::json;

struct Entry {
  std::string source;        // "ollama" | "hf"
  std::string id;            // "qwen3" or "Qwen/Qwen2.5-0.5B-Instruct-GGUF"
  std::string description;
  long downloads = 0, likes = 0;
};

struct Variant {
  std::string label;         // "8b" / "Q4_K_M"
  std::string file;          // HF: the .gguf file name
  double bytes = -1.0;       // -1 unknown
  std::string pullName;      // what Ollama pulls
};

std::string hfSearchUrl(const std::string& query);
std::string hfTreeUrl(const std::string& repo);
std::string ollamaSearchUrl(const std::string& query);
std::string ollamaTagsUrl(const std::string& name);

std::vector<Entry> parseHfSearch(const std::string& json);
std::vector<Variant> parseHfTree(const std::string& repo, const std::string& json);   // single-file GGUFs; split files skipped
std::vector<Entry> parseOllamaSearch(const std::string& html);
std::vector<Variant> parseOllamaTags(const std::string& name, const std::string& html);

std::string quantOf(const std::string& fileName);   // "model-q4_k_m.gguf" -> "Q4_K_M", "" when none
double parseSize(const std::string& text);          // "5.2GB" / "523MB" -> bytes, -1 when unreadable
std::string humanSize(double bytes);
// Ollama pulls into a blob store plus some headroom.
bool enoughSpace(double bytes, double freeBytes);

struct Progress {
  std::string status;
  double completed = 0.0, total = 0.0;
  bool done = false;
  std::string error;
};
Progress parseProgress(const Json& line);

}  // namespace modellib
