// The sampling panel: per-chat generation parameters (temperature, top-p,
// top-k, min-p, repeat penalty, max tokens, context size, seed, stop
// sequences) plus the system prompt, quick presets and a seed lock. Values
// live in chat.params under Open WebUI's own key names, so the web UI shows
// and applies the same settings; a key is only present while it is switched
// on, leaving the server's defaults for everything else.
#include "app.h"

#include <algorithm>
#include <random>

#include "core/common.h"
#include "core/outputctl.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string gPresetName;

int randomSeed() {
  static std::mt19937 rng{std::random_device{}()};
  return static_cast<int>(rng() % 1000000000u);
}

}  // namespace

bool App::hasSamplingParams() const {
  for (auto it = chat.params.begin(); it != chat.params.end(); ++it) {
    if (it.key() != "system") return true;
  }
  return outputctl::active(outputControls());
}

void App::drawSamplingPanel() {
  if (!ImGui::BeginPopup("##chatparams")) return;
  const auto& palette = theme::palette();
  const float width = 520.0f;
  bool changed = false;

  theme::pushBold();
  ImGui::TextUnformatted("Sampling for this chat");
  theme::pop();
  ImGui::SameLine();
  widgets::dimText("%s", selectedModels.empty() ? "" : selectedModels.front().c_str());

  // Quick presets write the few knobs that matter and clear the rest.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
  auto quick = [&](const char* label, float temperature, float topP, const char* tip) {
    if (ImGui::Button(label)) {
      const std::string system = chat.params.value("system", "");
      chat.params = Json::object();
      if (!system.empty()) chat.params["system"] = system;
      chat.params["temperature"] = temperature;
      chat.params["top_p"] = topP;
      changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine();
  };
  quick("Precise", 0.2f, 0.9f, "temperature 0.2, top-p 0.9: factual answers, code");
  quick("Balanced", 0.7f, 0.9f, "temperature 0.7, top-p 0.9");
  quick("Creative", 1.0f, 0.95f, "temperature 1.0, top-p 0.95: brainstorming, prose");
  if (ImGui::Button("Deterministic")) {
    const std::string system = chat.params.value("system", "");
    chat.params = Json::object();
    if (!system.empty()) chat.params["system"] = system;
    chat.params["temperature"] = 0.0f;
    chat.params["seed"] = 42;
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("temperature 0 with a fixed seed: the same prompt gives the same answer");
  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0, 4));

  ImGui::TextUnformatted("System prompt");
  std::string system = chat.params.value("system", "");
  if (ImGui::InputTextMultiline("##system", &system, ImVec2(width, 64))) {
    if (system.empty()) chat.params.erase("system");
    else chat.params["system"] = system;
    changed = true;
  }
  ImGui::Dummy(ImVec2(0, 4));

  // One row per parameter: a switch, a slider, and the value as text.
  auto floatOption = [&](const char* key, const char* label, float minimum, float maximum, float fallback,
                         const char* format, const char* tip) {
    bool enabled = chat.params.contains(key);
    float value = enabled ? chat.params.value(key, fallback) : fallback;
    ImGui::PushID(key);
    if (ImGui::Checkbox("##on", &enabled)) {
      if (enabled) chat.params[key] = value;
      else chat.params.erase(key);
      changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!enabled);
    ImGui::SetNextItemWidth(width - 190.0f);
    if (ImGui::SliderFloat("##v", &value, minimum, maximum, format) && enabled) {
      chat.params[key] = value;
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
  };
  auto intOption = [&](const char* key, const char* label, int minimum, int maximum, int fallback,
                       const char* tip, bool logarithmic) {
    bool enabled = chat.params.contains(key);
    int value = enabled ? chat.params.value(key, fallback) : fallback;
    ImGui::PushID(key);
    if (ImGui::Checkbox("##on", &enabled)) {
      if (enabled) chat.params[key] = value;
      else chat.params.erase(key);
      changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!enabled);
    ImGui::SetNextItemWidth(width - 190.0f);
    if (ImGui::SliderInt("##v", &value, minimum, maximum, "%d",
                         logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None) && enabled) {
      chat.params[key] = value;
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
  };

  floatOption("temperature", "Temperature", 0.0f, 2.0f, 0.8f, "%.2f",
              "Randomness of token choice. 0 is greedy; above 1 gets loose.");
  floatOption("top_p", "Top p", 0.0f, 1.0f, 0.9f, "%.2f",
              "Nucleus sampling: only the most likely tokens summing to p are considered.");
  intOption("top_k", "Top k", 1, 200, 40, "Only the k most likely tokens are considered.", false);
  floatOption("min_p", "Min p", 0.0f, 1.0f, 0.05f, "%.3f",
              "Drops tokens less likely than p times the top token; a cleaner alternative to top-p.");
  floatOption("repeat_penalty", "Repeat penalty", 0.5f, 2.0f, 1.1f, "%.2f",
              "Above 1 discourages repeating earlier tokens.");
  intOption("max_tokens", "Max tokens", 16, 32768, 2048, "Longest reply, in tokens.", true);
  {
    const int modelContext = selectedModels.empty() ? 0 : contextWindowFor(selectedModels.front());
    char tip[160];
    snprintf(tip, sizeof(tip), "Context window the model is run with. %s",
             modelContext > 0 ? ("This model reports " + std::to_string(modelContext) + ".").c_str()
                              : "Larger costs memory and time.");
    intOption("num_ctx", "Context size", 512, 131072, modelContext > 0 ? modelContext : 8192, tip, true);
  }

  // Seed: locked = present. Reroll draws a new one so the lock stays on.
  {
    bool locked = chat.params.contains("seed");
    int seed = locked ? chat.params.value("seed", 0) : 0;
    ImGui::PushID("seed");
    if (ImGui::Checkbox("##on", &locked)) {
      if (locked) chat.params["seed"] = randomSeed();
      else chat.params.erase("seed");
      changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!locked);
    ImGui::SetNextItemWidth(width - 290.0f);
    if (ImGui::InputInt("##v", &seed, 0, 0) && locked) {
      chat.params["seed"] = seed;
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reroll") && locked) {
      chat.params["seed"] = randomSeed();
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(locked ? "Seed (locked)" : "Seed");
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Locked: every regeneration uses this seed, so the same prompt and settings give the same answer.");
    }
    ImGui::PopID();
  }

  // Stop sequences, one per line.
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted("Stop sequences");
  ImGui::SameLine();
  widgets::dimText("one per line; generation ends when one appears");
  std::string stops;
  if (chat.params.contains("stop") && chat.params["stop"].is_array()) {
    for (const auto& s : chat.params["stop"]) {
      if (s.is_string()) stops += s.get<std::string>() + "\n";
    }
  }
  if (ImGui::InputTextMultiline("##stops", &stops, ImVec2(width, 48))) {
    Json list = Json::array();
    for (const std::string& line : core::split(stops, '\n')) {
      if (!line.empty()) list.push_back(line);
    }
    if (list.empty()) chat.params.erase("stop");
    else chat.params["stop"] = list;
    changed = true;
  }

  drawOutputControlsSection(width, &changed);

  ImGui::Separator();
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##presetname", "Preset name", &gPresetName);
  ImGui::SameLine();
  widgets::beginDisabled(core::trim(gPresetName).empty());
  if (ImGui::Button("Save as preset")) {
    saveCurrentAsPreset(core::trim(gPresetName));
    gPresetName.clear();
  }
  widgets::endDisabled(core::trim(gPresetName).empty());
  ImGui::SameLine();
  if (ImGui::Button("Reset all")) {
    chat.params = Json::object();
    changed = true;
  }
  ImGui::SameLine(width - 90.0f);
  if (widgets::primaryButton("Done", ImVec2(90, 0))) {
    saveActiveChat();
    ImGui::CloseCurrentPopup();
  }
  if (changed && !chat.id.empty()) saveActiveChat();
  (void)palette;
  ImGui::EndPopup();
}

}  // namespace app
