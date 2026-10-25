// Model evaluation in the app (#149): the Tools "Evaluate" tab - prompt sets,
// the models to run them across, a judge model, the run queue, the
// prompt x model grade matrix with thumbs overrides, per-model trends and
// CSV export. Sets live in support/eval/sets.json, scores in scores.jsonl.
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <set>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/eval.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<eval::PromptSet> gSets;
std::vector<eval::Score> gScores;
bool gLoaded = false;
int gSetIndex = 0;
std::set<std::string> gModels;
std::string gJudge;
bool gUseJudge = true;
std::atomic<bool> gRunning{false};
std::atomic<bool> gCancel{false};
int gDone = 0, gTotal = 0;
std::string gRunId;
std::string gNewName, gNewText, gNewExpected, gNewSetName;
std::string gSelectedPrompt, gSelectedModel;   // the matrix cell in the detail view
bool gShowAdd = false;

std::string evalDir() { return core::supportDir() + "/eval"; }

void load() {
  if (gLoaded) return;
  gLoaded = true;
  mkdir(evalDir().c_str(), 0700);
  bool ok = false;
  const std::string setsText = core::readFile(evalDir() + "/sets.json", &ok);
  if (ok) gSets = eval::setsFromJson(eval::Json::parse(setsText, nullptr, false));
  if (gSets.empty()) gSets.push_back(eval::defaultSet());
  const std::string scoresText = core::readFile(evalDir() + "/scores.jsonl", &ok);
  if (ok) gScores = eval::scoresFromJsonl(scoresText);
}

void saveSets() { core::writeFile(evalDir() + "/sets.json", eval::setsToJson(gSets).dump(2)); }

void appendScore(const eval::Score& s) {
  gScores.push_back(s);
  FILE* f = fopen((evalDir() + "/scores.jsonl").c_str(), "a");
  if (!f) return;
  const std::string line = eval::scoreToJson(s).dump() + "\n";
  fwrite(line.data(), 1, line.size(), f);
  fclose(f);
}

void rewriteScores() {
  std::string text;
  for (const eval::Score& s : gScores) text += eval::scoreToJson(s).dump() + "\n";
  core::writeFile(evalDir() + "/scores.jsonl", text);
}

ImU32 gradeColour(int grade) {
  const auto& palette = theme::palette();
  if (grade < 0) return palette.textDim;
  if (grade >= 7) return palette.success;
  if (grade >= 4) return palette.text;
  return palette.danger;
}

std::string answerText(const api::Result& res) {
  if (!res.ok) return "";
  const eval::Json& choices = res.data.value("choices", eval::Json::array());
  if (choices.is_array() && !choices.empty()) return choices[0].value("message", eval::Json::object()).value("content", "");
  return "";
}

}  // namespace

void App::runEval() {
  load();
  if (gRunning.load() || gSetIndex >= static_cast<int>(gSets.size())) return;
  const eval::PromptSet set = gSets[gSetIndex];
  if (set.prompts.empty() || gModels.empty()) {
    widgets::toastInfo("Pick at least one model and one prompt");
    return;
  }
  const std::vector<std::string> modelIds(gModels.begin(), gModels.end());
  const std::string judge = gUseJudge ? gJudge : "";
  gRunId = core::uuid().substr(0, 8);
  gRunning.store(true);
  gCancel.store(false);
  gDone = 0;
  gTotal = static_cast<int>(set.prompts.size() * modelIds.size());
  const std::string runId = gRunId;
  async::run([this, set, modelIds, judge, runId] {
    for (const std::string& model : modelIds) {
      for (const eval::Prompt& prompt : set.prompts) {
        if (gCancel.load()) break;
        eval::Score s;
        s.runId = runId;
        s.setId = set.id;
        s.promptId = prompt.id;
        s.model = model;
        s.judge = judge;
        const double started = core::nowMillis();
        api::Result res = api_.completion(eval::Json{{"model", model}, {"stream", false}, {"messages", eval::Json::array({eval::Json{{"role", "user"}, {"content", prompt.text}}})}});
        s.seconds = (core::nowMillis() - started) / 1000.0;
        s.at = core::nowSeconds();
        if (!res.ok) s.error = api::errorMessage(res);
        else s.answer = eval::stripThinking(answerText(res));
        if (!judge.empty() && res.ok && !gCancel.load()) {
          api::Result verdict = api_.completion(eval::Json{{"model", judge},
                                                           {"stream", false},
                                                           {"messages", eval::Json::array({eval::Json{{"role", "system"}, {"content", eval::judgeSystem()}},
                                                                                           eval::Json{{"role", "user"}, {"content", eval::judgeUser(prompt, s.answer)}}})}});
          if (verdict.ok) {
            int grade = -1;
            std::string reason;
            if (eval::parseJudge(answerText(verdict), &grade, &reason)) {
              s.grade = grade;
              s.reason = reason;
            } else {
              s.reason = "judge gave no verdict: " + eval::stripThinking(answerText(verdict)).substr(0, 120);
            }
          } else {
            s.reason = "judge call failed: " + api::errorMessage(verdict);
          }
        }
        async::post([s] {
          appendScore(s);
          ++gDone;
        });
      }
    }
    async::post([runId] {
      gRunning.store(false);
      widgets::toast(widgets::ToastLevel::Success, gCancel.load() ? "Evaluation stopped" : "Evaluation run " + runId + " finished");
    });
  });
}

void App::drawEvalSection(float width) {
  load();
  const auto& palette = theme::palette();
  widgets::sectionHeader("MODEL EVALUATION");
  ImGui::PushTextWrapPos(width - 10.0f);
  widgets::dimText("Run a prompt set across models, let a judge model grade every answer 0-10 (or grade by thumbs), and watch each model's score trend run by run. Everything is kept locally.");
  ImGui::PopTextWrapPos();

  // --- set
  if (gSetIndex >= static_cast<int>(gSets.size())) gSetIndex = 0;
  ImGui::SetNextItemWidth(240);
  if (ImGui::BeginCombo("Prompt set", gSets[gSetIndex].name.c_str())) {
    for (int i = 0; i < static_cast<int>(gSets.size()); ++i) {
      if (ImGui::Selectable((gSets[i].name + " (" + std::to_string(gSets[i].prompts.size()) + ")").c_str(), i == gSetIndex)) gSetIndex = i;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160);
  ImGui::InputTextWithHint("##newset", "new set name", &gNewSetName);
  ImGui::SameLine();
  if (ImGui::SmallButton("Add set") && !core::trim(gNewSetName).empty()) {
    eval::PromptSet s;
    s.id = core::uuid();
    s.name = core::trim(gNewSetName);
    gSets.push_back(s);
    gSetIndex = static_cast<int>(gSets.size()) - 1;
    gNewSetName.clear();
    saveSets();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Delete set") && gSets.size() > 1) {
    gSets.erase(gSets.begin() + gSetIndex);
    gSetIndex = 0;
    saveSets();
  }
  eval::PromptSet& set = gSets[gSetIndex];
  if (ImGui::TreeNodeEx("Prompts", ImGuiTreeNodeFlags_DefaultOpen, "Prompts (%zu)", set.prompts.size())) {
    int remove = -1;
    for (size_t i = 0; i < set.prompts.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      theme::pushBold();
      ImGui::TextUnformatted(set.prompts[i].name.c_str());
      theme::pop();
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
      ImGui::PushTextWrapPos(width - 10.0f);
      widgets::dimText("%s", set.prompts[i].text.c_str());
      if (!set.prompts[i].expected.empty()) widgets::dimText("Expect: %s", set.prompts[i].expected.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopID();
    }
    if (remove >= 0) {
      set.prompts.erase(set.prompts.begin() + remove);
      saveSets();
    }
    if (ImGui::SmallButton(gShowAdd ? "Hide" : "Add a prompt...")) gShowAdd = !gShowAdd;
    if (gShowAdd) {
      ImGui::SetNextItemWidth(260);
      ImGui::InputTextWithHint("##pname", "name", &gNewName);
      ImGui::InputTextMultiline("##ptext", &gNewText, ImVec2(width - 30.0f, 70.0f));
      ImGui::SetNextItemWidth(width - 30.0f);
      ImGui::InputTextWithHint("##pexp", "reference answer or rubric for the judge (optional)", &gNewExpected);
      if (ImGui::SmallButton("Save prompt") && !core::trim(gNewText).empty()) {
        eval::Prompt p;
        p.id = core::uuid();
        p.name = core::trim(gNewName).empty() ? core::trim(gNewText).substr(0, 40) : core::trim(gNewName);
        p.text = core::trim(gNewText);
        p.expected = core::trim(gNewExpected);
        set.prompts.push_back(p);
        gNewName.clear();
        gNewText.clear();
        gNewExpected.clear();
        saveSets();
      }
    }
    ImGui::TreePop();
  }

  // --- models and judge
  ImGui::Dummy(ImVec2(0, 6));
  theme::pushBold();
  ImGui::Text("Models to compare (%zu picked)", gModels.size());
  theme::pop();
  ImGui::BeginChild("##evalmodels", ImVec2(width - 20.0f, 110.0f), ImGuiChildFlags_Borders);
  for (const ModelInfo& m : models) {
    bool on = gModels.count(m.id) > 0;
    if (ImGui::Checkbox(m.id.c_str(), &on)) {
      if (on) gModels.insert(m.id);
      else gModels.erase(m.id);
    }
  }
  if (models.empty()) widgets::dimText("No models listed yet.");
  ImGui::EndChild();
  ImGui::Checkbox("Grade with a judge model", &gUseJudge);
  if (gUseJudge) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    if (gJudge.empty() && !models.empty()) gJudge = settings().extra.value("autoRouteSmallModel", std::string()).empty() ? models.front().id : settings().extra.value("autoRouteSmallModel", std::string());
    if (ImGui::BeginCombo("##judge", gJudge.c_str())) {
      for (const ModelInfo& m : models) {
        if (ImGui::Selectable(m.id.c_str(), m.id == gJudge)) gJudge = m.id;
      }
      ImGui::EndCombo();
    }
  }
  if (gRunning.load()) {
    widgets::beginDisabled(true);
    widgets::primaryButton(("Running " + std::to_string(gDone) + " / " + std::to_string(gTotal) + "...").c_str(), ImVec2(220, 0));
    widgets::endDisabled(true);
    ImGui::SameLine();
    if (ImGui::Button("Stop")) gCancel.store(true);
  } else {
    if (widgets::primaryButton("Run the set", ImVec2(220, 0))) runEval();
  }
  ImGui::SameLine();
  if (ImGui::Button("Export CSV...")) {
    const std::string path = platform::saveFile("model-evaluation.csv");
    if (!path.empty()) {
      core::writeFile(path, eval::csv(gScores));
      widgets::toast(widgets::ToastLevel::Success, "Saved " + std::to_string(gScores.size()) + " scores to " + path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy CSV")) {
    platform::setClipboardText(eval::csv(gScores));
    widgets::toast(widgets::ToastLevel::Success, "Copied " + std::to_string(gScores.size()) + " scores as CSV");
  }

  // --- matrix: latest run per prompt x model
  const auto matrix = eval::latest(gScores, set.id);
  std::set<std::string> modelCols;
  for (const auto& [promptId, byModel] : matrix) {
    for (const auto& [model, score] : byModel) modelCols.insert(model);
  }
  ImGui::Dummy(ImVec2(0, 8));
  widgets::sectionHeader("LATEST GRADES");
  if (modelCols.empty()) {
    widgets::dimText("No runs for this set yet.");
  } else {
    const std::vector<std::string> cols(modelCols.begin(), modelCols.end());
    const int n = static_cast<int>(std::min<size_t>(cols.size(), 12));
    if (ImGui::BeginTable("##evalmatrix", n + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 20.0f, 0))) {
      ImGui::TableSetupColumn("Prompt", ImGuiTableColumnFlags_WidthFixed, 180.0f);
      for (int c = 0; c < n; ++c) ImGui::TableSetupColumn(cols[c].c_str());
      ImGui::TableHeadersRow();
      for (const eval::Prompt& p : set.prompts) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(p.name.c_str());
        auto row = matrix.find(p.id);
        for (int c = 0; c < n; ++c) {
          ImGui::TableSetColumnIndex(c + 1);
          if (row == matrix.end() || !row->second.count(cols[c])) continue;
          const eval::Score& s = row->second.at(cols[c]);
          const int g = eval::effectiveGrade(s);
          std::string label = !s.error.empty() ? "error" : g < 0 ? "ungraded" : std::to_string(g) + "/10";
          char buf[32];
          snprintf(buf, sizeof(buf), "  %.1fs", s.seconds);
          label += buf;
          if (s.thumbs) label += s.thumbs > 0 ? "  +" : "  -";
          ImGui::PushID((p.id + cols[c]).c_str());
          ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(!s.error.empty() ? palette.danger : gradeColour(g)));
          if (ImGui::Selectable(label.c_str(), gSelectedPrompt == p.id && gSelectedModel == cols[c])) {
            gSelectedPrompt = p.id;
            gSelectedModel = cols[c];
          }
          ImGui::PopStyleColor();
          if (ImGui::IsItemHovered() && !s.reason.empty()) ImGui::SetTooltip("%s", s.reason.c_str());
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    // Detail for the picked cell.
    auto row = matrix.find(gSelectedPrompt);
    if (row != matrix.end() && row->second.count(gSelectedModel)) {
      const eval::Score picked = row->second.at(gSelectedModel);
      ImGui::Dummy(ImVec2(0, 4));
      theme::pushBold();
      ImGui::Text("%s - %s", gSelectedModel.c_str(), set.prompts.end() != std::find_if(set.prompts.begin(), set.prompts.end(), [&](const eval::Prompt& p) { return p.id == gSelectedPrompt; })
                                                         ? std::find_if(set.prompts.begin(), set.prompts.end(), [&](const eval::Prompt& p) { return p.id == gSelectedPrompt; })->name.c_str()
                                                         : gSelectedPrompt.c_str());
      theme::pop();
      widgets::dimText("run %s - %.1f s%s%s", picked.runId.c_str(), picked.seconds, picked.judge.empty() ? "" : (" - judged by " + picked.judge).c_str(),
                       picked.reason.empty() ? "" : (": " + picked.reason).c_str());
      if (!picked.error.empty()) widgets::dimText("error: %s", picked.error.c_str());
      ImGui::PushTextWrapPos(width - 10.0f);
      ImGui::TextUnformatted(picked.answer.empty() ? "(no answer)" : picked.answer.c_str());
      ImGui::PopTextWrapPos();
      auto setThumbs = [&](int value) {
        for (eval::Score& s : gScores) {
          if (s.runId == picked.runId && s.promptId == picked.promptId && s.model == picked.model) s.thumbs = s.thumbs == value ? 0 : value;
        }
        rewriteScores();
      };
      if (picked.thumbs > 0) ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(palette.success));
      if (ImGui::SmallButton("Good (10)")) setThumbs(1);
      if (picked.thumbs > 0) ImGui::PopStyleColor();
      ImGui::SameLine();
      if (picked.thumbs < 0) ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(palette.danger));
      if (ImGui::SmallButton("Bad (0)")) setThumbs(-1);
      if (picked.thumbs < 0) ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::SmallButton("Copy answer")) platform::setClipboardText(picked.answer);
      ImGui::SameLine();
      if (ImGui::SmallButton("Close")) gSelectedPrompt.clear();
    }
  }

  // --- trends
  ImGui::Dummy(ImVec2(0, 8));
  widgets::sectionHeader("TRENDS");
  const std::vector<eval::Trend> trends = eval::trends(gScores, set.id);
  if (trends.empty()) widgets::dimText("Grades over runs show here, one line per model, best mean first.");
  for (const eval::Trend& t : trends) {
    ImGui::PushID(t.model.c_str());
    theme::pushBold();
    ImGui::Text("%s", t.model.c_str());
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("mean %.1f - last %.1f - %d run%s - %.1f s per answer", t.mean, t.last, t.runs, t.runs == 1 ? "" : "s", t.meanSeconds);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float barW = 14.0f, gap = 3.0f, h = 36.0f;
    const size_t shown = std::min<size_t>(t.history.size(), 40);
    for (size_t i = t.history.size() - shown, x = 0; i < t.history.size(); ++i, ++x) {
      const float v = static_cast<float>(t.history[i] / 10.0);
      const float x0 = p.x + static_cast<float>(x) * (barW + gap);
      ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x0, p.y + h * (1.0f - v)), ImVec2(x0 + barW, p.y + h), gradeColour(static_cast<int>(t.history[i])), 2.0f);
    }
    ImGui::Dummy(ImVec2(static_cast<float>(shown) * (barW + gap), h + 4.0f));
    ImGui::PopID();
  }
}

}  // namespace app
