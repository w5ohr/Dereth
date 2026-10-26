#include "eval.h"

#include <algorithm>
#include <regex>
#include <set>

#include "common.h"

namespace eval {
namespace {

std::string str(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

std::string csvCell(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  return "\"" + core::replaceAll(s, "\"", "\"\"") + "\"";
}

}  // namespace

std::vector<PromptSet> setsFromJson(const Json& j) {
  std::vector<PromptSet> out;
  if (!j.is_array()) return out;
  for (const auto& s : j) {
    if (!s.is_object()) continue;
    PromptSet set;
    set.id = str(s, "id");
    set.name = str(s, "name");
    if (set.id.empty()) set.id = core::uuid();
    if (set.name.empty()) set.name = "Untitled set";
    const Json prompts = s.value("prompts", Json::array());
    if (prompts.is_array()) {
      for (const auto& p : prompts) {
        if (!p.is_object()) continue;
        Prompt prompt;
        prompt.id = str(p, "id");
        prompt.name = str(p, "name");
        prompt.text = str(p, "text");
        prompt.expected = str(p, "expected");
        if (prompt.text.empty()) continue;
        if (prompt.id.empty()) prompt.id = core::uuid();
        if (prompt.name.empty()) prompt.name = prompt.text.substr(0, 40);
        set.prompts.push_back(prompt);
      }
    }
    out.push_back(set);
  }
  return out;
}

Json setsToJson(const std::vector<PromptSet>& sets) {
  Json out = Json::array();
  for (const PromptSet& s : sets) {
    Json prompts = Json::array();
    for (const Prompt& p : s.prompts) prompts.push_back(Json{{"id", p.id}, {"name", p.name}, {"text", p.text}, {"expected", p.expected}});
    out.push_back(Json{{"id", s.id}, {"name", s.name}, {"prompts", prompts}});
  }
  return out;
}

PromptSet defaultSet() {
  PromptSet set;
  set.id = "starter";
  set.name = "Starter set";
  auto add = [&](const char* name, const char* text, const char* expected) {
    Prompt p;
    p.id = std::string("starter-") + std::to_string(set.prompts.size() + 1);
    p.name = name;
    p.text = text;
    p.expected = expected;
    set.prompts.push_back(p);
  };
  add("Arithmetic", "A train leaves at 09:40 and arrives at 13:05 the same day. How long is the journey? Answer with hours and minutes only.", "3 hours 25 minutes");
  add("Extraction", "From this sentence, list the people and their cities as JSON: \"Mara moved from Lisbon to Oslo in May, while Dev stayed in Pune.\"",
      "[{\"name\":\"Mara\",\"city\":\"Oslo\"},{\"name\":\"Dev\",\"city\":\"Pune\"}] - Lisbon is where Mara came from, not her city");
  add("Code", "Write a Python function is_leap(year) that returns True for leap years. Only the function, no explanation.",
      "Divisible by 4, except centuries unless divisible by 400; must be a single def with a return");
  add("Instruction following", "Reply with exactly three bullet points, each under eight words, naming a benefit of unit tests.", "Exactly three bullets, each under eight words");
  add("Summary", "Summarise in one sentence: 'The council voted 7-2 to extend the library's opening hours to 9 pm on weekdays from March, funded by a parking surcharge; opponents argued the surcharge hurts small shops.'",
      "One sentence covering the 7-2 vote, 9 pm weekday hours from March, the parking surcharge funding and the shop objection");
  return set;
}

Json scoreToJson(const Score& s) {
  return Json{{"runId", s.runId}, {"setId", s.setId}, {"promptId", s.promptId}, {"model", s.model}, {"answer", s.answer}, {"judge", s.judge}, {"reason", s.reason},
              {"seconds", s.seconds}, {"grade", s.grade}, {"thumbs", s.thumbs}, {"at", s.at}, {"error", s.error}};
}

Score scoreFromJson(const Json& j) {
  Score s;
  if (!j.is_object()) return s;
  s.runId = str(j, "runId");
  s.setId = str(j, "setId");
  s.promptId = str(j, "promptId");
  s.model = str(j, "model");
  s.answer = str(j, "answer");
  s.judge = str(j, "judge");
  s.reason = str(j, "reason");
  s.error = str(j, "error");
  if (j.contains("seconds") && j["seconds"].is_number()) s.seconds = j["seconds"].get<double>();
  if (j.contains("grade") && j["grade"].is_number_integer()) s.grade = std::clamp(j["grade"].get<int>(), -1, 10);
  if (j.contains("thumbs") && j["thumbs"].is_number_integer()) s.thumbs = std::clamp(j["thumbs"].get<int>(), -1, 1);
  if (j.contains("at") && j["at"].is_number_integer()) s.at = j["at"].get<int64_t>();
  return s;
}

std::vector<Score> scoresFromJsonl(const std::string& text) {
  std::vector<Score> out;
  for (const std::string& line : core::split(text, '\n')) {
    if (core::trim(line).empty()) continue;
    Json j = Json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) continue;
    Score s = scoreFromJson(j);
    if (!s.promptId.empty() && !s.model.empty()) out.push_back(s);
  }
  return out;
}

int effectiveGrade(const Score& s) {
  if (s.thumbs > 0) return 10;
  if (s.thumbs < 0) return 0;
  return s.grade;
}

std::string judgeSystem() {
  return "You are a strict, consistent grader of answers produced by language models. Score how well the answer completes the task, using the reference or rubric when one is given: "
         "10 = perfect and fully compliant, 7-9 = correct with small flaws, 4-6 = partly right or missing requirements, 1-3 = mostly wrong, 0 = empty, refused or off-topic. "
         "Reply with JSON only, nothing else: {\"score\": <integer 0-10>, \"reason\": \"<one short sentence>\"} /no_think";
}

std::string judgeUser(const Prompt& prompt, const std::string& answer) {
  std::string out = "Task given to the model:\n" + prompt.text + "\n\n";
  if (!prompt.expected.empty()) out += "Reference answer or rubric:\n" + prompt.expected + "\n\n";
  out += "The model's answer:\n" + (answer.empty() ? std::string("(empty)") : answer) + "\n\nJSON verdict:";
  return out;
}

std::string stripThinking(const std::string& text) {
  std::string out = text;
  size_t start;
  while ((start = out.find("<think>")) != std::string::npos) {
    const size_t end = out.find("</think>", start);
    if (end == std::string::npos) {
      out.erase(start);
      break;
    }
    out.erase(start, end + 8 - start);
  }
  return core::trim(out);
}

bool parseJudge(const std::string& textIn, int* grade, std::string* reason) {
  const std::string text = stripThinking(textIn);
  // JSON object anywhere (possibly fenced).
  size_t open = text.find('{');
  while (open != std::string::npos) {
    const size_t close = text.find('}', open);
    if (close == std::string::npos) break;
    Json j = Json::parse(text.substr(open, close - open + 1), nullptr, false);
    if (j.is_object() && j.contains("score")) {
      int g = -1;
      if (j["score"].is_number()) g = static_cast<int>(std::lround(j["score"].get<double>()));
      else if (j["score"].is_string()) g = atoi(j["score"].get<std::string>().c_str());
      if (g >= 0) {
        if (grade) *grade = std::clamp(g, 0, 10);
        if (reason) *reason = str(j, "reason");
        return true;
      }
    }
    open = text.find('{', open + 1);
  }
  static const std::regex kOutOfTen(R"((\d{1,2})\s*/\s*10)");
  static const std::regex kScore(R"([Ss]core\s*[:=]?\s*(\d{1,2}))");
  std::smatch m;
  if (std::regex_search(text, m, kOutOfTen) || std::regex_search(text, m, kScore)) {
    if (grade) *grade = std::clamp(atoi(m[1].str().c_str()), 0, 10);
    if (reason) *reason = core::trim(text.substr(0, std::min<size_t>(text.size(), 160)));
    return true;
  }
  return false;
}

std::map<std::string, std::map<std::string, Score>> latest(const std::vector<Score>& scores, const std::string& setId) {
  std::map<std::string, std::map<std::string, Score>> out;
  for (const Score& s : scores) {
    if (s.setId != setId) continue;
    Score& slot = out[s.promptId][s.model];
    if (slot.promptId.empty() || s.at >= slot.at) slot = s;
  }
  return out;
}

std::vector<Trend> trends(const std::vector<Score>& scores, const std::string& setId) {
  // model -> runId -> (sum, count, at, seconds sum)
  struct Acc {
    double sum = 0, seconds = 0;
    int count = 0;
    int64_t at = 0;
  };
  std::map<std::string, std::map<std::string, Acc>> acc;
  for (const Score& s : scores) {
    if (s.setId != setId) continue;
    const int g = effectiveGrade(s);
    if (g < 0) continue;
    Acc& a = acc[s.model][s.runId];
    a.sum += g;
    a.seconds += s.seconds;
    a.count++;
    a.at = std::max(a.at, s.at);
  }
  std::vector<Trend> out;
  for (const auto& [model, runs] : acc) {
    Trend t;
    t.model = model;
    std::vector<std::pair<int64_t, Acc>> ordered;
    for (const auto& [runId, a] : runs) ordered.emplace_back(a.at, a);
    std::sort(ordered.begin(), ordered.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    double secondsSum = 0;
    int answers = 0;
    for (const auto& [at, a] : ordered) {
      t.history.push_back(a.sum / a.count);
      secondsSum += a.seconds;
      answers += a.count;
    }
    t.runs = static_cast<int>(ordered.size());
    for (double h : t.history) t.mean += h;
    t.mean /= static_cast<double>(t.history.size());
    t.last = t.history.back();
    t.meanSeconds = answers ? secondsSum / answers : 0.0;
    out.push_back(t);
  }
  std::sort(out.begin(), out.end(), [](const Trend& a, const Trend& b) { return a.mean > b.mean; });
  return out;
}

std::string csv(const std::vector<Score>& scores) {
  std::string out = "at,run,set,prompt,model,grade,thumbs,effective,seconds,judge,reason,error,answer\n";
  for (const Score& s : scores) {
    out += core::formatTime(s.at, "%Y-%m-%d %H:%M:%S") + "," + csvCell(s.runId) + "," + csvCell(s.setId) + "," + csvCell(s.promptId) + "," + csvCell(s.model) + "," +
           std::to_string(s.grade) + "," + std::to_string(s.thumbs) + "," + std::to_string(effectiveGrade(s)) + "," + std::to_string(s.seconds) + "," + csvCell(s.judge) + "," +
           csvCell(s.reason) + "," + csvCell(s.error) + "," + csvCell(s.answer) + "\n";
  }
  return out;
}

}  // namespace eval
