// Local reranker (#105): a small local model re-scores retrieved chunks
// before they reach the prompt, so the four that go along are the four
// that answer the question. The pure parts: preferences, the scoring
// prompt, its answer parser and the ordering. App-free; --reranktest.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rerank {

using Json = nlohmann::json;

struct Prefs {
  bool enabled = false;
  int keep = 4;        // chunks that go along after reranking (1 .. 12)
  int minScore = 3;    // chunks scored below this are dropped (0 .. 10)
};
Prefs fromJson(const Json& extra);   // extra["localRerank"]
Json toJson(const Prefs& prefs);

struct Candidate {
  std::string id, title, text;
};
struct Scored {
  std::string id;
  int score = 0;   // 0 .. 10
};

std::string systemPrompt();
std::string userPrompt(const std::string& query, const std::vector<Candidate>& candidates, size_t maxCharsPerChunk = 700);
// JSON array of {"id": "3", "score": 8} (ids as numbers or strings), a bare
// positional array [8, 2, 5], or "3: 8" lines; think blocks ignored, scores clamped.
std::vector<Scored> parse(const std::string& reply, const std::vector<Candidate>& candidates);
// Ids by score (highest first), dropping those under minScore, at most `keep`.
std::vector<std::string> order(const std::vector<Scored>& scored, const Prefs& prefs);
std::string summary(const std::vector<Scored>& scored);   // "4 scored: 9, 8, 4, 1"

}  // namespace rerank
