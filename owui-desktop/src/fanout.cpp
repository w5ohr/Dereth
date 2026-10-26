// Fan-out: send one prompt to several models, read the answers in columns,
// keep the one you like. The answers are ordinary sibling messages under the
// user turn (what a multi-model send already produces), so the chat stays
// readable in the web UI; "keep" only picks the branch and, optionally,
// deletes the rest. Which turns are settled lives in chat.raw["fanout_kept"].
#include "app.h"

#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {

void App::fanoutSend(const std::string& text) {
  if (fanoutModels.size() < 2) {
    widgets::toastInfo("Pick at least two models to fan out to");
    return;
  }
  if (core::trim(text).empty() && attachments.empty()) return;
  // sendMessage() creates one assistant message per selected model; swap the
  // selection in for this one send and put the user's own back afterwards.
  const std::vector<std::string> saved = selectedModels;
  selectedModels = fanoutModels;
  sendMessage(core::trim(text));
  selectedModels = saved;
  ++composerGeneration;
  focusComposer = true;
}

void App::drawFanoutPicker() {
  if (showFanoutPicker) {
    ImGui::OpenPopup("##fanoutpick");
    showFanoutPicker = false;
  }
  if (!ImGui::BeginPopup("##fanoutpick")) return;
  widgets::sectionHeader("FAN OUT TO");
  widgets::dimText("The same prompt goes to every ticked model; answers appear side by side.");
  ImGui::Dummy(ImVec2(0, 4));
  bool changed = false;
  for (const ModelInfo& info : models) {
    bool ticked = std::find(fanoutModels.begin(), fanoutModels.end(), info.id) != fanoutModels.end();
    ImGui::PushID(info.id.c_str());
    if (ImGui::Checkbox(info.name.c_str(), &ticked)) {
      changed = true;
      if (ticked) {
        fanoutModels.push_back(info.id);
      } else {
        fanoutModels.erase(std::remove(fanoutModels.begin(), fanoutModels.end(), info.id), fanoutModels.end());
      }
    }
    ImGui::PopID();
  }
  if (changed) {
    settings().extra["fanoutModels"] = fanoutModels;
    settings().save();
  }
  ImGui::Dummy(ImVec2(0, 6));
  const bool ready = fanoutModels.size() >= 2 && (!core::trim(composer).empty() || !attachments.empty());
  widgets::beginDisabled(!ready);
  const std::string label = "Send to " + std::to_string(fanoutModels.size()) + " model" +
                            (fanoutModels.size() == 1 ? "" : "s");
  if (widgets::primaryButton(label.c_str(), ImVec2(200, 0))) {
    fanoutSend(composer);
    ImGui::CloseCurrentPopup();
  }
  widgets::endDisabled(!ready);
  if (!ready && fanoutModels.size() >= 2) widgets::dimText("Type the prompt in the composer first.");
  ImGui::EndPopup();
}

bool App::fanoutUnresolved(const std::string& parentId) const {
  auto parent = chat.messages.find(parentId);
  if (parent == chat.messages.end()) return false;
  int answers = 0;
  for (const std::string& child : parent->second.childrenIds) {
    auto it = chat.messages.find(child);
    if (it != chat.messages.end() && it->second.role == "assistant") ++answers;
  }
  if (answers < 2) return false;
  const Json kept = chat.raw.value("fanout_kept", Json::array());
  if (kept.is_array()) {
    for (const auto& id : kept) {
      if (id.is_string() && id.get<std::string>() == parentId) return false;
    }
  }
  return true;
}

void App::keepFanoutAnswer(const std::string& messageId, bool dropOthers) {
  auto it = chat.messages.find(messageId);
  if (it == chat.messages.end()) return;
  const std::string parentId = it->second.parentId;
  chat.currentId = messageId;
  if (dropOthers && chat.messages.count(parentId)) {
    std::vector<std::string>& children = chat.messages[parentId].childrenIds;
    for (const std::string& child : std::vector<std::string>(children)) {
      if (child == messageId) continue;
      auto sibling = chat.messages.find(child);
      if (sibling == chat.messages.end() || sibling->second.role != "assistant") continue;
      if (sibling->second.streaming) continue;   // leave a live stream alone
      chat.messages.erase(sibling);
      children.erase(std::remove(children.begin(), children.end(), child), children.end());
      // The server merges chat updates and keeps messages that merely
      // vanished from the body, so the drop has to be an explicit delete.
      if (!chat.id.empty()) {
        const std::string chatId = chat.id;
        async::run([this, chatId, child] { api_.deleteChatMessage(chatId, child); });
      }
    }
  }
  Json kept = chat.raw.value("fanout_kept", Json::array());
  if (!kept.is_array()) kept = Json::array();
  kept.push_back(parentId);
  chat.raw["fanout_kept"] = kept;
  saveActiveChat();
  scrollToBottom = true;
}

void App::drawFanoutColumns(const std::vector<std::string>& siblings, float width) {
  const auto& palette = theme::palette();
  const int n = static_cast<int>(siblings.size());
  if (n < 2) return;
  const float gap = 10.0f;
  const float minColumn = 300.0f;
  const bool scrolls = (width - gap * (n - 1)) / n < minColumn;
  const float column = scrolls ? minColumn : (width - gap * (n - 1)) / n;
  bool anyStreaming = false;
  for (const std::string& id : siblings) anyStreaming = anyStreaming || chat.messages.at(id).streaming;

  widgets::dimText("%d answers - keep one to continue the conversation from it", n);
  ImGui::BeginChild("##fanout", ImVec2(width, 0),
                    ImGuiChildFlags_AutoResizeY,
                    scrolls ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None);
  for (int i = 0; i < n; ++i) {
    const std::string& id = siblings[static_cast<size_t>(i)];
    Message& message = chat.messages.at(id);
    if (i > 0) ImGui::SameLine(0, gap);
    ImGui::PushID(id.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
    ImGui::BeginChild("##col", ImVec2(column, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                                                     ImGuiChildFlags_AlwaysUseWindowPadding);
    // Header: model, timing, keep.
    theme::pushBold();
    ImGui::TextUnformatted(message.modelName.empty() ? message.model.c_str() : message.modelName.c_str());
    theme::pop();
    auto timing = timings.find(id);
    if (timing != timings.end() && timing->second.firstTokenMs > 0.0) {
      ImGui::SameLine();
      const double ttft = (timing->second.firstTokenMs - timing->second.sentMs) / 1000.0;
      if (timing->second.doneMs > 0.0) {
        widgets::dimText("%.1fs", (timing->second.doneMs - timing->second.sentMs) / 1000.0);
      } else {
        widgets::dimText("first token %.1fs", ttft);
      }
    }
    if (message.usage.is_object() && message.usage.contains("response_token/s")) {
      ImGui::SameLine();
      widgets::dimText("%.0f tok/s", message.usage.value("response_token/s", 0.0));
    }
    if (message.streaming) {
      widgets::loadingRow(message.status.empty() ? "Thinking..." : message.status.c_str());
    } else {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
      if (widgets::primaryButton("Keep", ImVec2(70, 0))) keepFanoutAnswer(id, false);
      ImGui::SameLine();
      widgets::beginDisabled(anyStreaming);
      if (ImGui::Button("Keep, drop others", ImVec2(130, 0))) keepFanoutAnswer(id, true);
      widgets::endDisabled(anyStreaming);
      ImGui::PopStyleVar();
    }
    ImGui::Separator();

    // Body.
    if (!message.reasoning.empty()) {
      if (ImGui::CollapsingHeader(message.streaming ? "Thinking..." : "Reasoning")) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + column - 24.0f);
        ImGui::TextUnformatted(message.reasoning.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
      }
    }
    if (!message.toolCalls.empty()) drawToolCallCards(message, column - 24.0f);
    md::Options options;
    options.wrapWidth = column - 24.0f;
    options.compact = true;
    options.imageLoader = [this](const std::string& url) { return fetchImageBytes(url); };
    md::render("body", message.content, options);
    if (!message.error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + column - 24.0f);
      ImGui::TextUnformatted(message.error.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
  ImGui::EndChild();
}

}  // namespace app
