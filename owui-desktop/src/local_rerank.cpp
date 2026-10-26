// Local reranker in the app (#105): the small model re-scores local index
// hits before they go into the prompt and in the Local index tab; the
// switch and knobs live in that tab.
#include <map>

#include "app.h"
#include "core/common.h"
#include "core/rerank.h"
#include "core/settings.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::map<std::string, int> gLastScores;   // path -> score from the last rerank (for the tab)
std::string gLastSummary;

}  // namespace

rerank::Prefs App::rerankPrefs() const { return rerank::fromJson(core::Settings::instance().extra); }

std::string App::rerankModel() const {
  const std::string small = core::Settings::instance().extra.value("autoRouteSmallModel", std::string());
  if (!small.empty()) return small;
  return selectedModels.empty() ? std::string() : selectedModels.front();
}

// Blocking (one model call): only from a worker.
std::vector<std::pair<std::string, std::string>> App::rerankLocalHits(const std::string& query, const std::vector<std::pair<std::string, std::string>>& hits) {
  const rerank::Prefs p = rerankPrefs();
  if (!p.enabled || hits.empty()) return hits;
  const std::string model = rerankModel();
  if (model.empty()) return hits;
  std::vector<rerank::Candidate> candidates;
  for (size_t i = 0; i < hits.size(); ++i) {
    std::string title = hits[i].first;
    const size_t slash = title.rfind('/');
    if (slash != std::string::npos) title = title.substr(slash + 1);
    candidates.push_back({std::to_string(i + 1), title, hits[i].second});
  }
  const double started = core::nowMillis();
  api::Result res = api_.completion(Json{{"model", model},
                                         {"stream", false},
                                         {"messages", Json::array({Json{{"role", "system"}, {"content", rerank::systemPrompt()}}, Json{{"role", "user"}, {"content", rerank::userPrompt(query, candidates)}}})}});
  std::string text;
  if (res.ok) {
    const Json& choices = res.data.value("choices", Json::array());
    if (choices.is_array() && !choices.empty()) text = choices[0].value("message", Json::object()).value("content", "");
  }
  const std::vector<rerank::Scored> scored = rerank::parse(text, candidates);
  if (scored.empty()) {
    core::debugLog("rerank: no scores from %s (%s) - keeping the search order", model.c_str(), res.ok ? "unparsable reply" : api::errorMessage(res).c_str());
    gLastSummary = "rerank gave no scores - search order kept";
    return hits;
  }
  std::map<std::string, int> scores;
  for (const rerank::Scored& s : scored) scores[s.id] = s.score;
  gLastScores.clear();
  for (size_t i = 0; i < hits.size(); ++i) {
    auto it = scores.find(std::to_string(i + 1));
    if (it != scores.end()) gLastScores[hits[i].first + "\n" + hits[i].second.substr(0, 40)] = it->second;
  }
  std::vector<std::pair<std::string, std::string>> out;
  for (const std::string& id : rerank::order(scored, p)) {
    const size_t index = static_cast<size_t>(atoi(id.c_str())) - 1;
    if (index < hits.size()) out.push_back(hits[index]);
  }
  gLastSummary = rerank::summary(scored) + " by " + model + " in " + std::to_string(static_cast<int>(core::nowMillis() - started)) + " ms";
  core::debugLog("rerank: %s; kept %zu of %zu", gLastSummary.c_str(), out.size(), hits.size());
  return out;
}

int App::rerankScoreFor(const std::string& path, const std::string& text) const {
  auto it = gLastScores.find(path + "\n" + text.substr(0, 40));
  return it == gLastScores.end() ? -1 : it->second;
}

std::string App::rerankSummary() const { return gLastSummary; }

void App::drawRerankControls() {
  rerank::Prefs p = rerankPrefs();
  bool changed = false;
  changed |= ImGui::Checkbox("Re-rank hits with the small model before they reach the prompt (one short model call per search)", &p.enabled);
  if (p.enabled) {
    ImGui::SetNextItemWidth(160);
    changed |= ImGui::SliderInt("Keep", &p.keep, 1, 12);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    changed |= ImGui::SliderInt("Drop below", &p.minScore, 0, 10);
    ImGui::SameLine();
    widgets::dimText("model: %s", rerankModel().empty() ? "(none - pick a model)" : rerankModel().c_str());
  }
  if (changed) {
    settings().extra["localRerank"] = rerank::toJson(p);
    settings().save();
  }
}

}  // namespace app
