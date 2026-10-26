// Memory proposals in the app: after a server reply, a quiet model call
// (the reply's own model) lists durable facts the user stated in that turn;
// the candidates appear in a small card under the reply with Save / Edit /
// Skip, and only Save sends anything to the server's memories. Skipped
// texts are remembered locally so they are not proposed again.
#include <algorithm>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/memoryprop.h"
#include "imgui_stdlib.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr size_t kMaxSkipped = 200;

std::vector<std::string> memoryTexts(const Json& list) {
  std::vector<std::string> out;
  if (!list.is_array()) return out;
  for (const auto& m : list) {
    if (m.is_object() && m.contains("content") && m["content"].is_string()) out.push_back(m["content"]);
  }
  return out;
}

std::vector<std::string> skippedTexts(const Json& extra) {
  std::vector<std::string> out;
  const Json list = extra.value("memorySkipped", Json::array());
  if (!list.is_array()) return out;
  for (const auto& s : list) {
    if (s.is_string()) out.push_back(s);
  }
  return out;
}

}  // namespace

void App::maybeProposeMemories(const std::string& assistantId) {
  if (!settings().extra.value("memoryProposals", true) || chat.isPrivate) return;
  auto it = chat.messages.find(assistantId);
  if (it == chat.messages.end() || !it->second.error.empty() || it->second.content.empty()) return;
  if (memoryProposals.count(assistantId)) return;
  const Message& assistant = it->second;
  auto parent = chat.messages.find(assistant.parentId);
  if (parent == chat.messages.end() || parent->second.role != "user") return;
  const std::string userText = parent->second.content;
  if (core::trim(userText).size() < 40) return;   // "thanks" and the like carry nothing durable
  const std::string model = assistant.model.empty() ? (selectedModels.empty() ? "" : selectedModels.front()) : assistant.model;
  if (model.empty()) return;

  memoryProposals[assistantId] = {};   // marks the turn as asked; filled when the answer comes
  const std::string assistantText = assistant.content;
  const Json existingList = memoryList;
  const std::vector<std::string> skipped = skippedTexts(settings().extra);
  const std::string chatId = chat.id;
  async::run([this, assistantId, chatId, model, userText, assistantText, existingList, skipped] {
    std::vector<std::string> existing = memoryTexts(existingList);
    if (existing.empty()) {
      api::Result mem = api_.memories();
      if (mem.ok) existing = memoryTexts(mem.data);
    }
    const Json payload{{"model", model},
                       {"stream", false},
                       {"messages", Json::array({Json{{"role", "system"}, {"content", memoryprop::systemPrompt()}},
                                                 Json{{"role", "user"}, {"content", memoryprop::userPrompt(userText, assistantText, existing)}}})}};
    api::Result res = api_.completion(payload);
    std::vector<std::string> proposals;
    if (res.ok) {
      std::string text;
      const Json& choices = res.data.value("choices", Json::array());
      if (!choices.empty()) text = choices[0].value("message", Json::object()).value("content", "");
      proposals = memoryprop::filter(memoryprop::parse(text), existing, skipped, 3);
    } else {
      core::debugLog("memory proposals: model call failed: %s", api::errorMessage(res).c_str());
    }
    async::post([this, assistantId, chatId, proposals] {
      if (chat.id != chatId) {
        memoryProposals.erase(assistantId);   // the user moved on; ask again if they come back
        return;
      }
      std::vector<MemoryProposal>& list = memoryProposals[assistantId];
      for (const std::string& text : proposals) {
        MemoryProposal p;
        p.text = text;
        list.push_back(p);
      }
    });
  });
}

void App::acceptMemoryProposal(const std::string& assistantId, size_t index, const std::string& textIn) {
  const std::string text = core::trim(textIn);
  if (text.empty()) return;
  auto it = memoryProposals.find(assistantId);
  if (it == memoryProposals.end() || index >= it->second.size()) return;
  it->second[index].state = 3;   // saving
  async::run([this, assistantId, index, text] {
    api::Result res = api_.addMemory(text);
    async::post([this, assistantId, index, text, res] {
      auto it = memoryProposals.find(assistantId);
      if (it == memoryProposals.end() || index >= it->second.size()) return;
      if (res.ok) {
        it->second[index].state = 1;
        it->second[index].text = text;
        if (res.data.is_object()) memoryList.push_back(res.data);
        else memoryList.push_back(Json{{"content", text}});
        widgets::toast(widgets::ToastLevel::Success, "Remembered: " + text.substr(0, 60));
      } else {
        it->second[index].state = 0;
        widgets::toastError("Could not save the memory: " + api::errorMessage(res));
      }
    });
  });
}

void App::skipMemoryProposal(const std::string& assistantId, size_t index) {
  auto it = memoryProposals.find(assistantId);
  if (it == memoryProposals.end() || index >= it->second.size()) return;
  it->second[index].state = 2;
  Json list = settings().extra.value("memorySkipped", Json::array());
  if (!list.is_array()) list = Json::array();
  list.push_back(it->second[index].text);
  while (list.size() > kMaxSkipped) list.erase(list.begin());
  settings().extra["memorySkipped"] = list;
  settings().save();
}

void App::drawMemoryProposals(const std::string& assistantId) {
  auto it = memoryProposals.find(assistantId);
  if (it == memoryProposals.end()) return;
  std::vector<MemoryProposal>& list = it->second;
  bool anyOpen = false;
  for (const MemoryProposal& p : list) anyOpen = anyOpen || p.state == 0 || p.state == 3 || p.state == 1;
  if (!anyOpen) return;
  // Once everything is saved or skipped the card goes away; saved rows linger only while a sibling is still open.
  bool pending = false;
  for (const MemoryProposal& p : list) pending = pending || p.state == 0 || p.state == 3;
  if (!pending) return;

  const auto& palette = theme::palette();
  const float width = std::min(ImGui::GetContentRegionAvail().x, 760.0f);
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
  const float rowH = ImGui::GetTextLineHeightWithSpacing() * 1.9f;
  ImGui::BeginChild(("##memprop" + assistantId).c_str(), ImVec2(width, 34.0f + rowH * static_cast<float>(list.size())),
                    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize);
  ImGui::Dummy(ImVec2(0, 2));
  ImGui::Indent(10.0f);
  icons::inlineIcon(icons::Icon::Star, palette.accent);
  ImGui::SameLine(0, 6);
  theme::pushBold();
  ImGui::TextUnformatted("Remember this?");
  theme::pop();
  ImGui::SameLine();
  widgets::dimText("facts about you from this turn - saved to your memories only when you say so");
  ImGui::SameLine(width - 80.0f);
  if (ImGui::SmallButton("Skip all")) {
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i].state == 0) skipMemoryProposal(assistantId, i);
    }
  }
  for (size_t i = 0; i < list.size(); ++i) {
    MemoryProposal& p = list[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::Dummy(ImVec2(0, 3));
    if (p.state == 1) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.success));
      ImGui::TextWrapped("Saved: %s", p.text.c_str());
      ImGui::PopStyleColor();
    } else if (p.state == 2) {
      widgets::dimText("Skipped");
    } else if (p.editing) {
      ImGui::SetNextItemWidth(width - 200.0f);
      const bool enter = ImGui::InputText("##edit", &p.edit, ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      if (widgets::primaryButton("Save", ImVec2(70, 0)) || enter) {
        p.editing = false;
        acceptMemoryProposal(assistantId, i, p.edit);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Cancel")) p.editing = false;
    } else {
      ImGui::PushTextWrapPos(width - 230.0f);
      ImGui::TextUnformatted(p.text.c_str());
      ImGui::PopTextWrapPos();
      ImGui::SameLine(width - 215.0f);
      if (p.state == 3) {
        widgets::dimText("Saving...");
      } else {
        if (widgets::primaryButton("Save", ImVec2(60, 0))) acceptMemoryProposal(assistantId, i, p.text);
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit")) {
          p.editing = true;
          p.edit = p.text;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Skip")) skipMemoryProposal(assistantId, i);
      }
    }
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::Unindent(10.0f);
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

}  // namespace app
