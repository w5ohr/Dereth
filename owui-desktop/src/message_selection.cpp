// Message selection in the app: Shift-click a message header (or switch on
// selection mode for checkboxes) to pick a range, then act on it from the
// floating bar - copy, export, move to a new chat, delete - with the rest of
// the conversation re-linked around what was removed.
#include <set>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/msgselect.h"
#include "core/privatestore.h"
#include "core/store.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

msgselect::Graph graphOf(const ChatDoc& chat, const Json& user) {
  msgselect::Graph g;
  for (const auto& [id, m] : chat.messages) {
    msgselect::Node n;
    n.id = id;
    n.parentId = m.parentId;
    n.role = m.role;
    n.who = m.role == "user" ? user.value("name", "You") : (m.modelName.empty() ? "" : m.modelName);
    n.content = m.content;
    n.children = m.childrenIds;
    n.timestamp = m.timestamp;
    g[id] = n;
  }
  return g;
}

}  // namespace

std::vector<std::string> App::selectedInOrder() const {
  std::vector<std::string> out;
  for (const std::string& id : chat.branch()) {
    if (selectedMessages.count(id)) out.push_back(id);
  }
  return out;
}

void App::selectMessage(const std::string& id, bool range) {
  if (range && !selectionAnchor.empty()) {
    for (const std::string& m : msgselect::rangeBetween(chat.branch(), selectionAnchor, id)) selectedMessages.insert(m);
  } else if (selectedMessages.count(id)) {
    selectedMessages.erase(id);
  } else {
    selectedMessages.insert(id);
  }
  selectionAnchor = id;
}

void App::clearSelection() {
  selectedMessages.clear();
  selectionAnchor.clear();
  selectionMode = false;
}

void App::drawSelectionHandle(const std::string& messageId, const ImVec2& pos, float contentWidth) {
  const auto& palette = theme::palette();
  const bool selected = selectedMessages.count(messageId) > 0;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (selected) {
    dl->AddRectFilled(ImVec2(pos.x - 8.0f, pos.y - 3.0f), ImVec2(pos.x + contentWidth, pos.y + 23.0f),
                      (palette.accent & 0x00FFFFFF) | 0x30000000, 6.0f);
    dl->AddRectFilled(ImVec2(pos.x - 14.0f, pos.y - 3.0f), ImVec2(pos.x - 11.0f, pos.y + 23.0f), palette.accent, 2.0f);
  }
  if (selectionMode) {
    ImGui::SetCursorScreenPos(ImVec2(pos.x - 38.0f, pos.y));
    bool box = selected;
    ImGui::PushID("selbox");
    if (ImGui::Checkbox("##selbox", &box)) selectMessage(messageId, ImGui::GetIO().KeyShift);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(pos);
  }
  // Shift-click anywhere on the header line selects (a range from the anchor).
  if (ImGui::GetIO().KeyShift && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      ImGui::IsMouseHoveringRect(ImVec2(pos.x - 8.0f, pos.y - 3.0f), ImVec2(pos.x + contentWidth, pos.y + 23.0f))) {
    selectMessage(messageId, true);
  }
}

void App::drawSelectionBar(const ImVec2& transcriptMin, const ImVec2& transcriptMax) {
  if (selectedMessages.empty() && !selectionMode) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput) {
    clearSelection();
    return;
  }
  const auto& palette = theme::palette();
  const std::vector<std::string> ids = selectedInOrder();
  const float barW = 640.0f, barH = 40.0f;
  ImGui::SetNextWindowPos(ImVec2((transcriptMin.x + transcriptMax.x) * 0.5f - barW * 0.5f, transcriptMax.y - barH - 14.0f));
  ImGui::SetNextWindowSize(ImVec2(barW, barH));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::toVec4(palette.panelAlt));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
  if (ImGui::Begin("##selectionbar", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::AlignTextToFramePadding();
    theme::pushBold();
    ImGui::Text("%zu selected", ids.size());
    theme::pop();
    ImGui::SameLine();
    widgets::dimText(selectionMode ? "tick boxes, Shift-click for a range" : "Shift-click headers for a range");
    ImGui::SameLine(0, 14);
    widgets::beginDisabled(ids.empty());
    if (ImGui::SmallButton("Copy")) {
      ImGui::SetClipboardText(msgselect::markdown(graphOf(chat, user_), ids).c_str());
      widgets::toast(widgets::ToastLevel::Success, "Copied " + std::to_string(ids.size()) + " message" + (ids.size() == 1 ? "" : "s"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Export...")) {
      const std::string path = platform::saveFile("selection.md");
      if (!path.empty() && core::writeFile(path, "# " + chat.title + " (selection)\n\n" + msgselect::markdown(graphOf(chat, user_), ids))) {
        widgets::toast(widgets::ToastLevel::Success, "Saved to " + path);
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Move to new chat")) moveSelectionToNewChat();
    ImGui::SameLine();
    if (widgets::dangerButton("Delete", ImVec2(60, 0))) deleteSelection();
    widgets::endDisabled(ids.empty());
    ImGui::SameLine();
    if (ImGui::SmallButton(selectionMode ? "Done" : "Clear")) clearSelection();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void App::deleteSelection() {
  const std::vector<std::string> ids = selectedInOrder();
  if (ids.empty()) return;
  for (const std::string& id : ids) {
    auto it = chat.messages.find(id);
    if (it != chat.messages.end() && it->second.streaming) {
      widgets::toastInfo("A selected reply is still being written - stop it first");
      return;
    }
  }
  msgselect::Graph g = graphOf(chat, user_);
  std::string current = chat.currentId;
  for (const std::string& id : ids) msgselect::spliceOut(&g, id, &current);
  // Write the re-linked tree back.
  for (const std::string& id : ids) chat.messages.erase(id);
  for (auto& [id, m] : chat.messages) {
    auto node = g.find(id);
    if (node == g.end()) continue;
    m.parentId = node->second.parentId;
    m.childrenIds = node->second.children;
  }
  chat.currentId = current;
  for (const std::string& id : ids) {
    if (isPinned(id)) togglePin(id);
    memoryProposals.erase(id);
  }
  const size_t n = ids.size();
  clearSelection();
  if (chat.isPrivate || chat.id.empty()) {
    saveActiveChat();
  } else {
    // A chat update merges history.messages server-side, so the removed
    // nodes have to go through the delete endpoint first; the save that
    // follows writes the re-linked parents and children.
    const std::string chatId = chat.id;
    async::run([this, chatId, ids] {
      for (const std::string& id : ids) api_.deleteChatMessage(chatId, id);
      async::post([this, chatId] {
        if (chat.id == chatId) saveActiveChat();
      });
    });
  }
  widgets::toast(widgets::ToastLevel::Success, "Deleted " + std::to_string(n) + " message" + (n == 1 ? "" : "s") + " - the rest were joined up");
}

void App::moveSelectionToNewChat() {
  const std::vector<std::string> ids = selectedInOrder();
  if (ids.empty()) return;
  std::vector<std::string> fresh;
  for (size_t i = 0; i < ids.size(); ++i) fresh.push_back(core::uuid());
  const std::vector<msgselect::Node> chainNodes = msgselect::linearChain(graphOf(chat, user_), ids, fresh);
  if (chainNodes.empty()) return;

  ChatDoc doc;
  doc.models = chat.models;
  doc.isPrivate = chat.isPrivate;
  doc.backendId = chat.backendId;
  std::string firstUser;
  for (const msgselect::Node& n : chainNodes) {
    const Message& original = chat.messages.at(ids[&n - &chainNodes[0]]);
    Message m = original;
    m.id = n.id;
    m.parentId = n.parentId;
    m.childrenIds = n.children;
    m.streaming = false;
    m.serverStream = false;
    doc.messages[m.id] = m;
    if (firstUser.empty() && m.role == "user") firstUser = m.content;
  }
  doc.currentId = chainNodes.back().id;
  std::string title = core::trim(firstUser).substr(0, 48);
  if (title.empty()) title = "Moved messages";
  doc.title = "Moved: " + title;

  const size_t n = ids.size();
  if (chat.isPrivate) {
    // A private chat moves into another private chat; nothing touches the server.
    deleteSelection();
    const ChatDoc current = chat;
    chat = doc;
    chat.id = privatestore::newId();
    savePrivateChat();
    refreshPrivateChats();
    widgets::toast(widgets::ToastLevel::Success, "Moved " + std::to_string(n) + " message" + (n == 1 ? "" : "s") + " into a new private chat");
    return;
  }
  const Json body = doc.toJson();
  deleteSelection();
  async::run([this, body, n] {
    api::Result res = api_.createChat(body);
    async::post([this, res, n] {
      if (!res.ok || !res.data.contains("id")) {
        widgets::toastError("Could not create the new chat: " + api::errorMessage(res));
        return;
      }
      store::putChat(res.data);
      refreshChats(false);
      widgets::toast(widgets::ToastLevel::Success, "Moved " + std::to_string(n) + " message" + (n == 1 ? "" : "s") + " into a new chat");
      openChat(res.data["id"]);
    });
  });
}

}  // namespace app
