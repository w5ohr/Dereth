#include "recipes.h"

#include <regex>

#include "common.h"

namespace recipes {

Json Recipe::toJson() const {
  Json steps = Json::array();
  for (const Step& s : this->steps) steps.push_back(Json{{"prompt", s.prompt}, {"model", s.model}});
  return Json{{"id", id},           {"name", name},   {"description", description},
              {"model", model},     {"steps", steps}, {"newChatPerRun", newChatPerRun}};
}

Recipe Recipe::fromJson(const Json& j) {
  Recipe r;
  if (!j.is_object()) return r;
  r.id = j.value("id", "");
  r.name = j.value("name", "");
  r.description = j.value("description", "");
  r.model = j.value("model", "");
  r.newChatPerRun = j.value("newChatPerRun", true);
  if (j.contains("steps") && j["steps"].is_array()) {
    for (const auto& s : j["steps"]) {
      if (s.is_string()) r.steps.push_back({s.get<std::string>(), ""});
      else if (s.is_object()) r.steps.push_back({s.value("prompt", ""), s.value("model", "")});
    }
  }
  return r;
}

std::vector<promptvars::Var> inputs(const Recipe& recipe) {
  std::string all;
  for (const Step& s : recipe.steps) all += s.prompt + "\n";
  return promptvars::inputs(promptvars::parse(all));
}

std::string expandStep(const std::string& prompt, const std::vector<std::string>& replies,
                       const promptvars::Context& ctx) {
  // {{prev}} / {{step:N}} first; their contents are never re-scanned.
  static const std::regex re(R"(\{\{\s*(prev|step\s*:\s*(\d+))\s*\}\})", std::regex::icase);
  std::string out;
  size_t cursor = 0;
  std::vector<std::pair<size_t, std::string>> splices;   // where the reply text goes, so expand() skips it
  std::string templ;
  for (auto it = std::sregex_iterator(prompt.begin(), prompt.end(), re); it != std::sregex_iterator(); ++it) {
    const std::smatch& m = *it;
    templ.append(prompt, cursor, static_cast<size_t>(m.position(0)) - cursor);
    std::string value;
    if (m[2].matched) {
      const size_t n = static_cast<size_t>(std::atoi(m[2].str().c_str()));
      if (n >= 1 && n <= replies.size()) value = replies[n - 1];
    } else if (!replies.empty()) {
      value = replies.back();
    }
    // A placeholder that expand() leaves alone, swapped back afterwards.
    const std::string marker = "\x01R" + std::to_string(splices.size()) + "\x01";
    splices.emplace_back(0, value);
    templ += marker;
    cursor = static_cast<size_t>(m.position(0) + m.length(0));
  }
  templ.append(prompt, cursor, std::string::npos);
  out = promptvars::expand(templ, ctx).text;
  for (size_t i = 0; i < splices.size(); ++i) {
    const std::string marker = "\x01R" + std::to_string(i) + "\x01";
    size_t pos;
    while ((pos = out.find(marker)) != std::string::npos) out.replace(pos, marker.size(), splices[i].second);
  }
  return out;
}

std::vector<Recipe> examples() {
  std::vector<Recipe> out;
  {
    Recipe r;
    r.id = "example-summarise-and-act";
    r.name = "Summarise, then list actions";
    r.description = "Summarises the clipboard, then turns the summary into a checklist.";
    r.steps = {{"Summarise this in five bullets:\n\n{{clipboard}}", ""},
               {"Turn that summary into a checklist of concrete next actions, each starting with a verb:\n\n{{prev}}", ""}};
    out.push_back(r);
  }
  {
    Recipe r;
    r.id = "example-draft-critique-rewrite";
    r.name = "Draft, critique, rewrite";
    r.description = "Writes a first draft, has it critiqued, then rewrites it with the critique applied.";
    r.steps = {{"Write a first draft of: {{input:What to write|a short announcement}}", ""},
               {"Critique this draft in five short points - what is unclear, missing or weak:\n\n{{prev}}", ""},
               {"Rewrite the draft applying every point of the critique. Draft:\n\n{{step:1}}\n\nCritique:\n\n{{step:2}}", ""}};
    out.push_back(r);
  }
  {
    Recipe r;
    r.id = "example-explain-and-quiz";
    r.name = "Explain, then quiz me";
    r.description = "Explains a topic, then writes three questions about the explanation.";
    r.steps = {{"Explain {{input:Topic}} to a curious beginner in under 200 words.", ""},
               {"Write three short questions that test whether someone understood this explanation, with answers at the end:\n\n{{prev}}", ""}};
    out.push_back(r);
  }
  return out;
}

}  // namespace recipes
