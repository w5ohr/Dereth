// Retrieval inspector in the app (#104): a side column for a reply's
// retrieved chunks - source, page, relevance, pin / exclude, open - and a
// re-ask that sends the question back with only the chosen passages while
// the server's own retrieval is skipped for that turn.
#include "app.h"
#include "core/common.h"
#include "core/retrieval.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string gExpanded;   // chunk shown in full

}  // namespace

void App::toggleRetrievalColumn(const std::string& messageId) {
  if (showRetrieval && retrievalMessageId == messageId) {
    showRetrieval = false;
    return;
  }
  retrievalMessageId = messageId;
  showRetrieval = true;
}

retrieval::Marks App::retrievalMarks(const std::string& messageId) const {
  const Json all = chat.raw.value("desktop_retrieval", Json::object());
  return retrieval::marksFromJson(all.is_object() ? all.value(messageId, Json::object()) : Json::object());
}

void App::saveRetrievalMarks(const std::string& messageId, const retrieval::Marks& marks) {
  Json& all = chat.raw["desktop_retrieval"];
  if (!all.is_object()) all = Json::object();
  if (marks.pinned.empty() && marks.excluded.empty()) all.erase(messageId);
  else all[messageId] = retrieval::marksToJson(marks);
  saveActiveChat();
}

void App::drawRetrievalButton(const Message& message) {
  if (message.sources.empty() || retrieval::fromSources(message.sources).empty()) return;
  ImGui::SameLine();
  const bool active = showRetrieval && retrievalMessageId == message.id;
  if (icons::button("retrieval", icons::Icon::Layers, "Retrieval: what was retrieved, its relevance, pin or exclude passages, re-ask", 0.0f,
                    active ? theme::palette().accent : 0)) {
    toggleRetrievalColumn(message.id);
  }
}

void App::reaskWithPassages(const std::string& messageId) {
  auto it = chat.messages.find(messageId);
  if (it == chat.messages.end()) return;
  const Message& reply = it->second;
  std::string question;
  auto parent = chat.messages.find(reply.parentId);
  if (parent != chat.messages.end()) question = parent->second.content;
  if (question.empty()) {
    widgets::toastError("Could not find the question this reply answered");
    return;
  }
  const std::vector<retrieval::Chunk> chunks = retrieval::fromSources(reply.sources);
  const std::vector<retrieval::Chunk> passages = retrieval::passagesForReask(chunks, retrievalMarks(messageId));
  if (passages.empty()) {
    widgets::toastInfo("Pin or exclude a passage first");
    return;
  }
  retrievalBypassNext_ = true;
  sendMessage(retrieval::reaskPrompt(question, passages));
  widgets::toastInfo("Re-asked with " + std::to_string(passages.size()) + " passage" + (passages.size() == 1 ? "" : "s") + " - the server's retrieval is skipped for this turn");
}

void App::drawRetrievalColumn(float width) {
  const auto& palette = theme::palette();
  ImGui::BeginChild("##retrieval", ImVec2(width, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(10.0f);
  widgets::sectionHeader("RETRIEVAL");
  auto it = chat.messages.find(retrievalMessageId);
  if (it == chat.messages.end()) {
    widgets::dimText("Open a reply's retrieval from the layers button under it.");
    if (ImGui::SmallButton("Close")) showRetrieval = false;
    ImGui::Unindent(10.0f);
    ImGui::EndChild();
    return;
  }
  const Message& reply = it->second;
  const std::vector<retrieval::Chunk> chunks = retrieval::ranked(retrieval::fromSources(reply.sources));
  retrieval::Marks marks = retrievalMarks(retrievalMessageId);
  const retrieval::Stats st = retrieval::stats(chunks, marks);

  if (ImGui::SmallButton("Close")) showRetrieval = false;
  ImGui::SameLine();
  theme::pushBold();
  ImGui::Text("%d chunk%s from %d source%s", st.chunks, st.chunks == 1 ? "" : "s", st.sources, st.sources == 1 ? "" : "s");
  theme::pop();
  if (st.scored > 0) widgets::dimText("Relevance: mean %s, best %s (the server's similarity score; higher is closer)", retrieval::relevanceLabel(st.mean).c_str(), retrieval::relevanceLabel(st.best).c_str());
  else widgets::dimText("The server sent no relevance scores for these chunks.");
  ImGui::PushTextWrapPos(width - 10.0f);
  widgets::dimText("Pin the passages the answer should rest on, or exclude the noise, then re-ask: the question goes back with only those passages quoted and the server's retrieval is skipped for that turn.");
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, 4));

  const std::vector<retrieval::Chunk> passages = retrieval::passagesForReask(chunks, marks);
  const std::string label = !marks.pinned.empty() ? "Re-ask with " + std::to_string(passages.size()) + " pinned passage" + (passages.size() == 1 ? "" : "s")
                            : !marks.excluded.empty() ? "Re-ask without the " + std::to_string(st.excluded) + " excluded"
                                                      : "Re-ask (pin or exclude first)";
  ImGui::BeginDisabled(passages.empty());
  if (widgets::primaryButton(label.c_str(), ImVec2(width - 24.0f, 0))) reaskWithPassages(retrievalMessageId);
  ImGui::EndDisabled();
  if (ImGui::SmallButton("Copy passages")) {
    platform::setClipboardText(retrieval::copyText(passages.empty() ? chunks : passages));
    widgets::toast(widgets::ToastLevel::Success, passages.empty() ? "Copied every passage" : "Copied the chosen passages");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(st.pinned + st.excluded == 0);
  if (ImGui::SmallButton("Clear marks")) saveRetrievalMarks(retrievalMessageId, retrieval::Marks{});
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, 6));

  ImGui::BeginChild("##chunks", ImVec2(width - 14.0f, 0));
  for (const retrieval::Chunk& c : chunks) {
    ImGui::PushID(c.id.c_str());
    const bool pinned = marks.pinned.count(c.id) > 0;
    const bool excluded = marks.excluded.count(c.id) > 0;
    theme::pushBold();
    ImGui::PushTextWrapPos(width - 30.0f);
    if (c.page > 0) ImGui::Text("%s - p. %d", c.name.c_str(), c.page);
    else ImGui::TextUnformatted(c.name.c_str());
    ImGui::PopTextWrapPos();
    theme::pop();
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = width - 110.0f;
      ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 6.0f), palette.panelAlt, 3.0f);
      if (c.relevance >= 0) ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w * static_cast<float>(c.relevance), p.y + 6.0f), excluded ? palette.textDim : palette.accent, 3.0f);
      ImGui::Dummy(ImVec2(w, 6.0f));
      ImGui::SameLine();
      widgets::dimText("%s%s%s", retrieval::relevanceLabel(c.relevance).c_str(), pinned ? " - pinned" : "", excluded ? " - excluded" : "");
    }
    const bool expanded = gExpanded == c.id;
    std::string text = c.text;
    if (!expanded && text.size() > 240) text = text.substr(0, 240) + "...";
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(excluded ? palette.textDim : palette.text));
    ImGui::PushTextWrapPos(width - 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (c.text.size() > 240) {
      if (ImGui::SmallButton(expanded ? "Less" : "More")) gExpanded = expanded ? "" : c.id;
      ImGui::SameLine();
    }
    if (pinned) ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(palette.accent));
    if (ImGui::SmallButton(pinned ? "Pinned" : "Pin")) {
      retrieval::togglePin(marks, c.id);
      saveRetrievalMarks(retrievalMessageId, marks);
    }
    if (pinned) ImGui::PopStyleColor();
    ImGui::SameLine();
    if (excluded) ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(palette.danger));
    if (ImGui::SmallButton(excluded ? "Excluded" : "Exclude")) {
      retrieval::toggleExclude(marks, c.id);
      saveRetrievalMarks(retrievalMessageId, marks);
    }
    if (excluded) ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open") && reply.sources.is_array() && c.source < static_cast<int>(reply.sources.size())) openCitation(reply.sources[c.source], c.index);
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::Unindent(10.0f);
  ImGui::EndChild();
}

}  // namespace app
