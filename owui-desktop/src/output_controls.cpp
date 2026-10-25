// Grammar and tool-choice controls in the app (#80): the per-chat record in
// chat.raw["outputControls"], its application to the tool loop and the
// server payload, and the "Output and tools" block of the sampling panel.
#include "app.h"
#include "core/common.h"
#include "core/outputctl.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {

outputctl::Controls App::outputControls() const { return outputctl::fromJson(chat.raw); }

void App::setOutputControls(const outputctl::Controls& c) {
  if (!chat.raw.is_object()) chat.raw = Json::object();
  if (outputctl::active(c)) chat.raw["outputControls"] = outputctl::toJson(c);
  else chat.raw.erase("outputControls");
}

void App::applyOutputControls(agents::LoopConfig& config) {
  const outputctl::Controls c = outputControls();
  if (!outputctl::active(c)) return;
  std::vector<std::string> names;
  for (const auto& spec : config.tools) names.push_back(spec.name);
  if (c.mode == "json" && config.format.is_null()) config.format = "json";
  if (c.mode == "grammar" && !core::trim(c.grammar).empty()) config.extra["grammar"] = c.grammar;
  if (c.toolChoice == "none") {
    config.tools.clear();
  } else if (c.toolChoice != "auto" && !config.tools.empty()) {
    const std::string forced = outputctl::forcedTool(c.toolChoice);
    if (forced.empty() || std::find(names.begin(), names.end(), forced) != names.end()) config.extra["tool_choice"] = outputctl::openaiToolChoice(c.toolChoice);
  }
  const std::string note = outputctl::nudge(c, names);
  if (!note.empty()) config.systemPrefix += (config.systemPrefix.empty() ? "" : "\n\n") + note;
}

void App::drawOutputControlsSection(float width, bool* changed) {
  const auto& palette = theme::palette();
  outputctl::Controls c = outputControls();
  bool dirty = false;
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted("Output and tools");
  ImGui::SameLine();
  widgets::dimText("%s", outputctl::summary(c).c_str());
  const bool schemaOn = structuredEnabled();
  widgets::beginDisabled(schemaOn);
  int mode = c.mode == "json" ? 1 : c.mode == "grammar" ? 2 : 0;
  if (ImGui::RadioButton("Free text", &mode, 0)) dirty = true;
  ImGui::SameLine();
  if (ImGui::RadioButton("JSON object", &mode, 1)) dirty = true;
  ImGui::SameLine();
  if (ImGui::RadioButton("GBNF grammar", &mode, 2)) dirty = true;
  widgets::endDisabled(schemaOn);
  if (schemaOn) widgets::dimText("Structured output's schema is on for this chat and takes precedence.");
  c.mode = mode == 1 ? "json" : mode == 2 ? "grammar" : "free";
  if (c.mode == "grammar") {
    if (ImGui::InputTextMultiline("##gbnf", &c.grammar, ImVec2(width, 96))) dirty = true;
    if (ImGui::IsItemHovered() && c.grammar.empty()) ImGui::SetTooltip("root ::= \"yes\" | \"no\"");
    const std::string error = outputctl::validateGrammar(c.grammar);
    if (!error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::TextWrapped("%s", error.c_str());
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.success));
      ImGui::Text("Grammar ok: %d rule%s", outputctl::ruleCount(c.grammar), outputctl::ruleCount(c.grammar) == 1 ? "" : "s");
      ImGui::PopStyleColor();
    }
    ImGui::PushTextWrapPos(width);
    widgets::dimText("Sent as `grammar`; llama.cpp-style servers (llama-server, LM Studio) honour it. Ollama and Anthropic ignore it, and the server passes it only to OpenAI-style models.");
    ImGui::PopTextWrapPos();
  } else if (c.mode == "json") {
    ImGui::PushTextWrapPos(width);
    widgets::dimText("Ollama `format: json` / `response_format: json_object`, plus a system line for models that only follow the prompt.");
    ImGui::PopTextWrapPos();
  }

  std::vector<std::string> names;
  for (const auto& spec : mcpToolSpecs()) names.push_back(spec.name);
  std::vector<std::string> choices{"auto", "none", "required"};
  std::vector<std::string> labels{"Auto - the model decides", "None - no tools this chat", "Required - a tool call first"};
  for (const std::string& n : names) {
    choices.push_back("tool:" + n);
    labels.push_back("Force " + n);
  }
  int current = 0;
  for (size_t i = 0; i < choices.size(); ++i) {
    if (choices[i] == c.toolChoice) current = static_cast<int>(i);
  }
  if (current == 0 && c.toolChoice != "auto") {
    choices.push_back(c.toolChoice);
    labels.push_back("Force " + outputctl::forcedTool(c.toolChoice) + " (not connected)");
    current = static_cast<int>(choices.size()) - 1;
  }
  ImGui::SetNextItemWidth(width - 110.0f);
  if (ImGui::BeginCombo("Tool choice", labels[static_cast<size_t>(current)].c_str())) {
    for (size_t i = 0; i < choices.size(); ++i) {
      if (ImGui::Selectable(labels[i].c_str(), static_cast<int>(i) == current)) {
        c.toolChoice = choices[i];
        dirty = true;
      }
    }
    ImGui::EndCombo();
  }
  if (c.toolChoice != "auto") {
    ImGui::PushTextWrapPos(width);
    widgets::dimText(c.toolChoice == "none" ? "Tools are left out of every request in this chat."
                                            : "Sent as `tool_choice` to OpenAI- and Anthropic-style backends; Ollama has none, so the model is told to call the tool in a system line and the request after the call runs free.");
    ImGui::PopTextWrapPos();
  }
  if (names.empty() && c.toolChoice != "auto" && c.toolChoice != "none") widgets::dimText("No MCP tools are connected right now.");
  if (dirty) {
    setOutputControls(c);
    if (changed) *changed = true;
  }
}

}  // namespace app
