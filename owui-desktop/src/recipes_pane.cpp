// Recipes in the app: Tools > Recipes edits them (settings extra["recipes"]),
// the palette runs them ("Run recipe: ..."), a schedule entry can point at
// one, and the runner is a small state machine ticked every frame: a fresh
// chat, then each step sent as a message once the previous reply finished,
// with {{prev}} / {{step:N}} filled from the replies so far.
#include "app.h"

#include <algorithm>

#include "core/common.h"
#include "core/recipes.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Editor {
  bool open = false;
  bool isNew = false;
  recipes::Recipe recipe;
};
Editor gEditor;

struct Run {
  bool active = false;
  recipes::Recipe recipe;
  size_t step = 0;                 // next step to send
  std::vector<std::string> replies;
  std::string awaiting;            // assistant message id of the step in flight
  std::map<std::string, std::string> inputs;
  bool askingInputs = false;
  bool inputsJustOpened = false;
  std::vector<promptvars::Var> fields;
  std::vector<std::string> values;
  std::string chatId;
  double startedAt = 0.0;
  std::vector<std::string> attachPaths;   // attached to the first step
  bool waitingForUploads = false;
};
Run gRun;

std::string newRecipeId() { return "recipe-" + core::uuid().substr(0, 8); }

}  // namespace

std::vector<recipes::Recipe> App::recipeList() const {
  std::vector<recipes::Recipe> out;
  const Json& extra = core::Settings::instance().extra;
  if (extra.contains("recipes") && extra["recipes"].is_array()) {
    for (const auto& j : extra["recipes"]) out.push_back(recipes::Recipe::fromJson(j));
  }
  return out;
}

void App::saveRecipe(const recipes::Recipe& recipe) {
  Json& list = settings().extra["recipes"];
  if (!list.is_array()) list = Json::array();
  bool replaced = false;
  for (auto& j : list) {
    if (j.is_object() && j.value("id", "") == recipe.id) {
      j = recipe.toJson();
      replaced = true;
    }
  }
  if (!replaced) list.push_back(recipe.toJson());
  settings().save();
}

void App::deleteRecipe(const std::string& id) {
  Json& list = settings().extra["recipes"];
  if (!list.is_array()) return;
  Json kept = Json::array();
  for (const auto& j : list) {
    if (!(j.is_object() && j.value("id", "") == id)) kept.push_back(j);
  }
  list = kept;
  settings().save();
}

// ---------------------------------------------------------------- running

void App::runRecipe(const std::string& id) {
  if (gRun.active) {
    widgets::toastInfo("A recipe is already running - stop it first (Tools > Recipes)");
    return;
  }
  recipes::Recipe found;
  for (const recipes::Recipe& r : recipeList()) {
    if (r.id == id) found = r;
  }
  if (found.id.empty()) {
    for (const recipes::Recipe& r : recipes::examples()) {
      if (r.id == id) found = r;
    }
  }
  if (found.id.empty() || found.steps.empty()) {
    widgets::toastError("Recipe not found or has no steps");
    return;
  }
  gRun = Run{};
  gRun.recipe = found;
  gRun.fields = recipes::inputs(found);
  if (!gRun.fields.empty()) {
    for (const promptvars::Var& f : gRun.fields) gRun.values.push_back(f.def);
    gRun.askingInputs = true;
    gRun.inputsJustOpened = true;
    gRun.active = true;
    pane = Pane::Chat;
    return;
  }
  startRecipeRun();
}

void App::startRecipeRun() {
  gRun.active = true;
  gRun.askingInputs = false;
  gRun.startedAt = core::nowMillis();
  if (gRun.recipe.newChatPerRun || chat.id.empty()) {
    newChat();
    chat.title = "Recipe: " + gRun.recipe.name;
  }
  pane = Pane::Chat;
  widgets::toastInfo("Running recipe \"" + gRun.recipe.name + "\" (" + std::to_string(gRun.recipe.steps.size()) + " steps)");
  if (!gRun.attachPaths.empty()) {
    attachFiles(gRun.attachPaths);
    gRun.attachPaths.clear();
    gRun.waitingForUploads = true;   // step 1 goes once the upload is done (tickRecipe)
    return;
  }
  recipeSendNext();
}

void App::runRecipeWith(const std::string& id, const std::map<std::string, std::string>& inputs,
                        const std::vector<std::string>& attachPaths) {
  if (gRun.active) return;
  recipes::Recipe found;
  for (const recipes::Recipe& r : recipeList()) {
    if (r.id == id) found = r;
  }
  if (found.id.empty() || found.steps.empty()) {
    widgets::toastError("Trigger: recipe not found or has no steps");
    return;
  }
  gRun = Run{};
  gRun.recipe = found;
  gRun.inputs = inputs;
  // Inputs the trigger did not supply take their defaults.
  for (const promptvars::Var& f : recipes::inputs(found)) {
    if (!gRun.inputs.count(f.arg)) gRun.inputs[f.arg] = f.def;
  }
  gRun.attachPaths = attachPaths;
  startRecipeRun();
}

void App::recipeSendNext() {
  if (!gRun.active || gRun.step >= gRun.recipe.steps.size()) return;
  const recipes::Step& step = gRun.recipe.steps[gRun.step];
  const std::string model = !step.model.empty() ? step.model : gRun.recipe.model;
  if (!model.empty()) selectedModels = {model};
  promptvars::Context ctx = promptContext();
  ctx.inputs = gRun.inputs;
  const std::string text = recipes::expandStep(step.prompt, gRun.replies, ctx);
  if (core::trim(text).empty()) {
    widgets::toastError("Recipe step " + std::to_string(gRun.step + 1) + " came out empty - stopped");
    gRun.active = false;
    return;
  }
  sendingExpanded_ = true;   // the step's variables are already filled in
  sendMessage(text);
  sendingExpanded_ = false;
  gRun.awaiting = chat.currentId;   // the assistant placeholder
  gRun.chatId = chat.id;
  ++gRun.step;
}

void App::tickRecipe() {
  if (gRun.active && gRun.waitingForUploads) {
    for (const Attachment& a : attachments) {
      if (a.uploading) return;
    }
    gRun.waitingForUploads = false;
    recipeSendNext();
    return;
  }
  if (!gRun.active || gRun.askingInputs || gRun.awaiting.empty()) return;
  auto it = chat.messages.find(gRun.awaiting);
  if (it == chat.messages.end()) {
    // The chat changed under the run: stop quietly.
    gRun.active = false;
    widgets::toastInfo("Recipe stopped - the chat changed");
    return;
  }
  if (it->second.streaming || !streams_.empty()) return;
  if (!it->second.error.empty()) {
    gRun.active = false;
    widgets::toastError("Recipe stopped at step " + std::to_string(gRun.step) + ": " + it->second.error);
    return;
  }
  gRun.replies.push_back(it->second.content);
  gRun.awaiting.clear();
  if (gRun.step >= gRun.recipe.steps.size()) {
    gRun.active = false;
    widgets::toast(widgets::ToastLevel::Success, "Recipe \"" + gRun.recipe.name + "\" finished (" +
                                                     std::to_string(gRun.replies.size()) + " steps)");
    return;
  }
  recipeSendNext();
}

void App::stopRecipe() {
  if (!gRun.active) return;
  gRun.active = false;
  gRun.awaiting.clear();
  stopAllStreams();
  widgets::toastInfo("Recipe stopped");
}

bool App::recipeRunning() const { return gRun.active; }

// The inputs form, drawn from the frame like the other modals.
void App::drawRecipeInputs() {
  if (!gRun.active || !gRun.askingInputs) return;
  if (gRun.inputsJustOpened) {
    ImGui::OpenPopup("Recipe inputs");
    gRun.inputsJustOpened = false;
  }
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  bool keepOpen = true;
  if (!ImGui::BeginPopupModal("Recipe inputs", &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
    if (!keepOpen) gRun.active = false;
    return;
  }
  theme::pushBold();
  ImGui::TextUnformatted(gRun.recipe.name.c_str());
  theme::pop();
  if (!gRun.recipe.description.empty()) widgets::dimText("%s", gRun.recipe.description.c_str());
  bool submit = false;
  for (size_t i = 0; i < gRun.fields.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::TextUnformatted(gRun.fields[i].arg.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (i == 0 && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("##v", &gRun.values[i], ImGuiInputTextFlags_EnterReturnsTrue)) submit = true;
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0, 6));
  if (widgets::primaryButton("Run", ImVec2(100, 0))) submit = true;
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    gRun.active = false;
    ImGui::CloseCurrentPopup();
  }
  if (submit) {
    for (size_t i = 0; i < gRun.fields.size(); ++i) gRun.inputs[gRun.fields[i].arg] = gRun.values[i];
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    startRecipeRun();
    return;
  }
  ImGui::EndPopup();
}

// ---------------------------------------------------------------- editor + list

void App::drawRecipesSection(float width) {
  const auto& palette = theme::palette();
  widgets::sectionHeader("RECIPES");
  widgets::dimText("A recipe chains prompts: each step can use {{prev}} (the last reply), {{step:N}}, and the prompt\n"
                   "variables ({{clipboard}}, {{date}}, {{input:Label}} - asked once before the run). Run from here, the\n"
                   "palette (\"Run recipe: ...\"), or a schedule.");
  if (gRun.active) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.accent));
    ImGui::Text("Running \"%s\": step %zu of %zu", gRun.recipe.name.c_str(), std::min(gRun.step, gRun.recipe.steps.size()),
                gRun.recipe.steps.size());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop")) stopRecipe();
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
  if (ImGui::Button("New recipe")) {
    gEditor = Editor{};
    gEditor.open = true;
    gEditor.isNew = true;
    gEditor.recipe.id = newRecipeId();
    gEditor.recipe.steps = {{"", ""}};
  }
  ImGui::SameLine();
  if (ImGui::Button("Add the examples")) {
    for (const recipes::Recipe& r : recipes::examples()) saveRecipe(r);
    widgets::toastInfo("Three example recipes added");
  }
  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0, 6));

  const std::vector<recipes::Recipe> list = recipeList();
  if (list.empty()) widgets::dimText("No recipes yet.");
  std::string removeId;
  for (const recipes::Recipe& r : list) {
    ImGui::PushID(r.id.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
    ImGui::BeginChild("##recipe", ImVec2(width - 20.0f, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::pushBold();
    ImGui::TextUnformatted(r.name.empty() ? "(unnamed)" : r.name.c_str());
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("%zu step%s%s%s", r.steps.size(), r.steps.size() == 1 ? "" : "s", r.model.empty() ? "" : " - ",
                     r.model.c_str());
    if (!r.description.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 60.0f);
      ImGui::TextUnformatted(r.description.c_str());
      ImGui::PopTextWrapPos();
    }
    for (size_t i = 0; i < r.steps.size(); ++i) {
      std::string line = r.steps[i].prompt;
      for (char& c : line) {
        if (c == '\n') c = ' ';
      }
      if (line.size() > 110) line = line.substr(0, 109) + "...";
      widgets::dimText("%zu. %s", i + 1, line.c_str());
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));
    widgets::beginDisabled(gRun.active);
    if (widgets::primaryButton("Run", ImVec2(70, 0))) runRecipe(r.id);
    widgets::endDisabled(gRun.active);
    ImGui::SameLine();
    if (widgets::ghostButton("Edit")) {
      gEditor = Editor{};
      gEditor.open = true;
      gEditor.recipe = r;
    }
    ImGui::SameLine();
    if (widgets::ghostButton("Schedule daily 08:00")) {
      Json& schedules = settings().extra["schedules"];
      if (!schedules.is_array()) schedules = Json::array();
      ScheduledPrompt s;
      s.id = core::uuid();
      s.name = "Recipe: " + r.name;
      s.prompt = "recipe:" + r.id;
      s.model = r.model;
      schedules.push_back(s.toJson());
      settings().save();
      widgets::toastInfo("Scheduled daily at 08:00 - adjust it under Settings > Scheduled prompts");
    }
    ImGui::SameLine();
    if (widgets::ghostButton("Delete")) removeId = r.id;
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4));
  }
  if (!removeId.empty()) deleteRecipe(removeId);

  ImGui::Dummy(ImVec2(0, 10));
  drawTriggersSection(width);

  // Editor modal.
  if (gEditor.open) {
    ImGui::OpenPopup("Recipe");
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Recipe", &gEditor.open)) {
      recipes::Recipe& r = gEditor.recipe;
      ImGui::SetNextItemWidth(300);
      ImGui::InputTextWithHint("##name", "Name", &r.name);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(200);
      ImGui::InputTextWithHint("##model", "model (blank = current)", &r.model);
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##desc", "What it does (optional)", &r.description);
      ImGui::Checkbox("Start a new chat for each run", &r.newChatPerRun);
      ImGui::Separator();
      ImGui::BeginChild("##steps", ImVec2(0, -44));
      int removeStep = -1;
      for (size_t i = 0; i < r.steps.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("Step %zu", i + 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##stepmodel", "model (optional)", &r.steps[i].model);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove") && r.steps.size() > 1) removeStep = static_cast<int>(i);
        ImGui::InputTextMultiline("##prompt", &r.steps[i].prompt, ImVec2(-1.0f, 84.0f));
        ImGui::PopID();
      }
      if (removeStep >= 0) r.steps.erase(r.steps.begin() + removeStep);
      if (ImGui::SmallButton("+ Add step")) r.steps.push_back({"", ""});
      ImGui::EndChild();
      widgets::beginDisabled(core::trim(r.name).empty());
      if (widgets::primaryButton("Save", ImVec2(100, 0))) {
        saveRecipe(r);
        gEditor.open = false;
        ImGui::CloseCurrentPopup();
      }
      widgets::endDisabled(core::trim(r.name).empty());
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        gEditor.open = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      widgets::dimText("{{prev}}  {{step:N}}  {{clipboard}}  {{input:Label|default}}  {{date}}  {{selection}}");
      ImGui::EndPopup();
    }
  }
}

}  // namespace app
