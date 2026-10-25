#include "rerank.h"

#include <algorithm>
#include <regex>

#include "common.h"

namespace rerank {

Prefs fromJson(const Json& extra) {
  Prefs p;
  const Json j = extra.is_object() ? extra.value("localRerank", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", p.enabled);
  p.keep = std::clamp(j.value("keep", p.keep), 1, 12);
  p.minScore = std::clamp(j.value("minScore", p.minScore), 0, 10);
  return p;
}

Json toJson(const Prefs& p) { return Json{{"enabled", p.enabled}, {"keep", p.keep}, {"minScore", p.minScore}}; }

std::string systemPrompt() {
  return "You grade how useful text passages are for answering a question. For every passage give an integer score from 0 (irrelevant) to 10 (answers the question directly). "
         "Reply with JSON only: [{\"id\": \"1\", \"score\": 7}, ...] covering every id. /no_think";
}

std::string userPrompt(const std::string& query, const std::vector<Candidate>& candidates, size_t maxCharsPerChunk) {
  std::string out = "Question: " + core::trim(query) + "\n\nPassages:\n";
  for (const Candidate& c : candidates) {
    std::string text = core::trim(c.text);
    if (text.size() > maxCharsPerChunk) {
      size_t cut = maxCharsPerChunk;
      while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
      text = text.substr(0, cut) + "...";
    }
    out += "\n[" + c.id + "] " + (c.title.empty() ? "" : c.title + "\n") + text + "\n";
  }
  return out + "\nScores as JSON:";
}

std::vector<Scored> parse(const std::string& replyIn, const std::vector<Candidate>& candidates) {
  std::string reply = replyIn;
  size_t think;
  while ((think = reply.find("<think>")) != std::string::npos) {
    const size_t end = reply.find("</think>", think);
    reply.erase(think, end == std::string::npos ? std::string::npos : end + 8 - think);
  }
  auto known = [&](const std::string& id) {
    for (const Candidate& c : candidates) {
      if (c.id == id) return true;
    }
    return false;
  };
  std::vector<Scored> out;
  auto push = [&](const std::string& id, double score) {
    if (!known(id)) return;
    for (const Scored& s : out) {
      if (s.id == id) return;
    }
    out.push_back({id, std::clamp(static_cast<int>(std::lround(score)), 0, 10)});
  };
  const size_t open = reply.find('[');
  const size_t close = open == std::string::npos ? std::string::npos : reply.rfind(']');
  if (open != std::string::npos && close != std::string::npos && close > open) {
    Json arr = Json::parse(reply.substr(open, close - open + 1), nullptr, false);
    if (arr.is_array()) {
      bool objects = false, positional = true;
      for (const auto& item : arr) {
        if (item.is_object()) objects = true;
        if (!item.is_number()) positional = false;
      }
      if (objects) {
        for (const auto& item : arr) {
          if (!item.is_object()) continue;
          std::string id;
          if (item.contains("id")) id = item["id"].is_string() ? item["id"].get<std::string>() : item["id"].is_number() ? std::to_string(item["id"].get<int>()) : "";
          double score = -1;
          if (item.contains("score") && item["score"].is_number()) score = item["score"].get<double>();
          else if (item.contains("score") && item["score"].is_string()) score = atof(item["score"].get<std::string>().c_str());
          if (!id.empty() && score >= 0) push(id, score);
        }
        if (!out.empty()) return out;
      } else if (positional && !arr.empty() && arr.size() == candidates.size()) {
        // A bare list of numbers only counts when it has one score per passage
        // ("[2] - 3" in prose is an id, not a list).
        for (size_t i = 0; i < arr.size(); ++i) push(candidates[i].id, arr[i].get<double>());
        if (!out.empty()) return out;
      }
    }
  }
  static const std::regex kLine(R"((?:^|\n)[ \t]*\[?[ \t]*(\w+)[ \t]*\]?[ \t]*[:=-][ \t]*(\d{1,2}))");   // one line each; never swallow a newline
  for (auto it = std::sregex_iterator(reply.begin(), reply.end(), kLine); it != std::sregex_iterator(); ++it) push((*it)[1].str(), atof((*it)[2].str().c_str()));
  return out;
}

std::vector<std::string> order(const std::vector<Scored>& scored, const Prefs& prefs) {
  std::vector<Scored> sorted = scored;
  std::stable_sort(sorted.begin(), sorted.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
  std::vector<std::string> out;
  for (const Scored& s : sorted) {
    if (s.score < prefs.minScore) continue;
    if (static_cast<int>(out.size()) >= prefs.keep) break;
    out.push_back(s.id);
  }
  return out;
}

std::string summary(const std::vector<Scored>& scored) {
  if (scored.empty()) return "nothing scored";
  std::vector<Scored> sorted = scored;
  std::stable_sort(sorted.begin(), sorted.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
  std::string out = std::to_string(sorted.size()) + " scored: ";
  for (size_t i = 0; i < sorted.size(); ++i) out += (i ? ", " : "") + std::to_string(sorted[i].score);
  return out;
}

}  // namespace rerank
