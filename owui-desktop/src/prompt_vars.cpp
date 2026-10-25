// Prompt variables in the app: sendMessage hands a prompt with {{...}} in it
// to interceptPromptVariables, which expands the live values straight away
// or, when the prompt asks for {{input:Label}} fields, opens a fill-in form
// first. The form previews the expanded prompt as the fields are typed.
#include "app.h"

#include "core/common.h"
#include "core/promptvars.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {

promptvars::Context App::promptContext() const {
  promptvars::Context ctx;
  ctx.clipboard = [] { return platform::clipboardText(); };
  ctx.selection = [this] {
    // The terminal pane's selection, else the text last handed in through
    // Services ("Ask Open WebUI") or the Quick Ask window.
    const std::string terminal = terminalSelectionText();
    return terminal.empty() ? lastSelectionText : terminal;
  };
  ctx.file = [this](const std::string& spec) {
    std::string path = core::trim(spec);
    if (path.empty()) return std::string();
    if (path[0] == '~') path = native::homeDirectory() + path.substr(1);
    if (path[0] != '/') {
      // Relative paths resolve against the project folder, then home.
      const std::string base = projectFolder.empty() ? native::homeDirectory() : projectFolder;
      path = base + "/" + path;
    }
    bool ok = false;
    const std::string bytes = core::readFile(path, &ok);
    return ok ? bytes : std::string();
  };
  ctx.user = user_.value("name", "");
  ctx.model = selectedModels.empty() ? "" : selectedModels.front();
  ctx.chatTitle = chat.title;
  return ctx;
}

bool App::interceptPromptVariables(const std::string& text) {
  if (!promptvars::hasVariables(text)) return false;
  const std::vector<promptvars::Var> vars = promptvars::parse(text);
  const std::vector<promptvars::Var> fields = promptvars::inputs(vars);
  if (fields.empty()) {
    const promptvars::Expansion expansion = promptvars::expand(text, promptContext());
    sendExpandedPrompt(expansion, text);
    return true;
  }
  promptForm = PromptForm{};
  promptForm.open = true;
  promptForm.justOpened = true;
  promptForm.text = text;
  promptForm.fields = fields;
  for (const promptvars::Var& field : fields) promptForm.values.push_back(field.def);
  return true;
}

// Sends the expanded prompt; the history keeps the template so it can be
// reused with fresh values.
void App::sendExpandedPrompt(const promptvars::Expansion& expansion, const std::string& original) {
  historyOverride_ = original;
  sendingExpanded_ = true;
  sendMessage(expansion.text);
  sendingExpanded_ = false;
  historyOverride_.clear();
  if (!expansion.notes.empty()) {
    std::string note;
    for (const std::string& n : expansion.notes) {
      if (!note.empty()) note += " - ";
      note += n;
    }
    widgets::toastInfo(note);
  }
}

void App::drawPromptForm() {
  if (!promptForm.open) return;
  if (promptForm.justOpened) {
    ImGui::OpenPopup("Fill in the prompt");
    promptForm.justOpened = false;
  }
  const auto& palette = theme::palette();
  ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
  bool keepOpen = true;
  if (!ImGui::BeginPopupModal("Fill in the prompt", &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
    if (!keepOpen) promptForm.open = false;
    return;
  }

  promptvars::Context ctx = promptContext();
  for (size_t i = 0; i < promptForm.fields.size(); ++i) {
    const promptvars::Var& field = promptForm.fields[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::TextUnformatted(field.arg.empty() ? "Input" : field.arg.c_str());
    if (!field.def.empty()) {
      ImGui::SameLine();
      widgets::dimText("(default: %s)", field.def.c_str());
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (i == 0 && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    // Enter in the last field sends; a multi-line value wants Shift+Enter.
    const bool enter = ImGui::InputText("##value", &promptForm.values[i], ImGuiInputTextFlags_EnterReturnsTrue);
    if (enter) promptForm.submit = true;
    ImGui::PopID();
    ctx.inputs[field.arg] = promptForm.values[i];
    ImGui::Dummy(ImVec2(0, 4));
  }

  // Live preview of what will be sent.
  const promptvars::Expansion expansion = promptvars::expand(promptForm.text, ctx);
  widgets::sectionHeader("WILL SEND");
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
  ImGui::BeginChild("##preview", ImVec2(0, 160), ImGuiChildFlags_None);
  theme::pushMono();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 8.0f);
  std::string shown = expansion.text;
  if (shown.size() > 4000) shown = shown.substr(0, 4000) + "\n[...]";
  ImGui::TextUnformatted(shown.c_str());
  ImGui::PopTextWrapPos();
  theme::pop();
  ImGui::EndChild();
  ImGui::PopStyleColor();
  for (const std::string& note : expansion.notes) widgets::dimText("%s", note.c_str());

  ImGui::Dummy(ImVec2(0, 6));
  if (widgets::primaryButton("Send", ImVec2(100, 0))) promptForm.submit = true;
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    promptForm.open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  widgets::dimText("Enter sends - the template stays in your prompt history");

  if (promptForm.submit) {
    promptForm.submit = false;
    promptForm.open = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    sendExpandedPrompt(expansion, promptForm.text);
    ++composerGeneration;
    focusComposer = true;
    return;
  }
  ImGui::EndPopup();
}

}  // namespace app
