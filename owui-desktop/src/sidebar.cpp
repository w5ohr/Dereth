// Left sidebar: new chat, search, folders, pinned and recent conversations.
#include <algorithm>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/store.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string gRenameId;
std::string gRenameBuffer;
std::string gPendingDelete;
std::string gNewFolderName;
bool gShowArchived = false;

const char* bucketFor(int64_t updatedAt) {
  const int64_t age = core::nowSeconds() - updatedAt;
  if (age < 86400) return "Today";
  if (age < 172800) return "Yesterday";
  if (age < 604800) return "Previous 7 days";
  if (age < 2592000) return "Previous 30 days";
  return "Older";
}

}  // namespace

void App::drawSidebar(float width) {
  const auto& palette = theme::palette();
  ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_None);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetWindowPos();
  dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + ImGui::GetWindowHeight()),
                    palette.panel);
  dl->AddLine(ImVec2(origin.x + width - 1, origin.y),
              ImVec2(origin.x + width - 1, origin.y + ImGui::GetWindowHeight()), palette.border);

  ImGui::Dummy(ImVec2(0, 26));
  ImGui::Indent(12.0f);
  const float inner = width - 24.0f;

  if (widgets::primaryButton("New chat", ImVec2(inner - 40.0f, 34))) newChat();
  ImGui::SameLine();
  if (icons::button("newprivate", icons::Icon::Lock,
                    "New private chat: encrypted on this Mac, replies straight from Ollama, never sent to the server",
                    0.0f, chat.isPrivate ? palette.accent : 0)) {
    newPrivateChat();
  }
  ImGui::Dummy(ImVec2(0, 8));

  if (widgets::searchField("chatsearch", &chatSearch, "Search chats", inner)) {
    if (chatSearch.size() >= 2) {
      const std::string query = chatSearch;
      // The local copy answers instantly and is all there is offline; the
      // server's results replace it when they arrive.
      auto fromMirror = [this](const std::string& q) {
        chats.clear();
        for (const store::ChatRow& row : store::search(q)) {
          ChatSummary summary;
          summary.id = row.id;
          summary.title = row.title.empty() ? "Untitled" : row.title;
          summary.updatedAt = row.updatedAt;
          summary.pinned = row.pinned;
          chats.push_back(summary);
        }
      };
      fromMirror(query);
      async::run([this, query, fromMirror] {
        api::Result res = api_.chatSearch(query, 1);
        async::post([this, res, query, fromMirror] {
          if (chatSearch != query) return;
          if (!res.ok || !res.data.is_array()) {
            fromMirror(query);
            return;
          }
          chats.clear();
          for (const auto& item : res.data) {
            ChatSummary summary;
            summary.id = item.value("id", "");
            summary.title = item.value("title", "Untitled");
            summary.updatedAt = item.value("updated_at", static_cast<int64_t>(0));
            summary.pinned = item.value("pinned", false);
            chats.push_back(summary);
          }
        });
      });
    } else if (chatSearch.empty()) {
      refreshChats(false);
    }
  }

  ImGui::Dummy(ImVec2(0, 10));
  if (offlineList) {
    widgets::pill("Offline - showing the local copy", theme::palette().panelAlt, theme::palette().warning);
    ImGui::Dummy(ImVec2(0, 6));
  }

  ImGui::BeginChild("##chatlist", ImVec2(inner, -10));

  // --- folders -------------------------------------------------------------
  if (!folders.empty()) {
    widgets::sectionHeader("FOLDERS");
    for (const Json& folder : folders) {
      const std::string folderId = folder.value("id", "");
      const std::string name = folder.value("name", "Folder");
      ImGui::PushID(folderId.c_str());
      ImVec2 pos = ImGui::GetCursorScreenPos();
      icons::draw(ImGui::GetWindowDrawList(), icons::Icon::Folder,
                  ImVec2(pos.x + 8, pos.y + ImGui::GetTextLineHeight() * 0.6f), 13.0f,
                  palette.textDim);
      ImGui::Indent(20.0f);
      if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (const ChatSummary& summary : chats) {
          if (summary.folderId != folderId) continue;
          if (ImGui::Selectable(summary.title.c_str(), summary.id == chat.id)) {
            openChat(summary.id);
          }
        }
        ImGui::TreePop();
      }
      ImGui::Unindent(20.0f);
      if (ImGui::BeginPopupContextItem("foldermenu")) {
        if (ImGui::MenuItem("Delete folder")) {
          async::run([this, folderId] {
            api_.deleteFolder(folderId);
            async::post([this] { refreshChats(false); });
          });
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6));
  }

  if (ImGui::SmallButton("+ New folder")) ImGui::OpenPopup("##newfolder");
  if (ImGui::BeginPopup("##newfolder")) {
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputTextWithHint("##foldername", "Folder name", &gNewFolderName,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
      const std::string name = gNewFolderName;
      gNewFolderName.clear();
      async::run([this, name] {
        api_.createFolder(name);
        async::post([this] { refreshChats(false); });
      });
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // --- conversations -------------------------------------------------------
  std::vector<const ChatSummary*> pinned;
  std::vector<const ChatSummary*> loose;
  for (const ChatSummary& summary : chats) {
    if (!summary.folderId.empty()) continue;
    if (summary.archived != gShowArchived) continue;
    (summary.pinned ? pinned : loose).push_back(&summary);
  }
  std::sort(loose.begin(), loose.end(),
            [](const ChatSummary* a, const ChatSummary* b) { return a->updatedAt > b->updatedAt; });

  auto drawRow = [&](const ChatSummary& summary) {
    ImGui::PushID(summary.id.c_str());
    const bool active = summary.id == chat.id;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetTextLineHeight() + 12.0f;

    if (gRenameId == summary.id) {
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputText("##rename", &gRenameBuffer,
                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        const std::string id = summary.id;
        const std::string title = gRenameBuffer;
        async::run([this, id, title] {
          api::Result current = api_.chat(id);
          if (current.ok) {
            Json body = current.data.value("chat", Json::object());
            body["title"] = title;
            api_.updateChat(id, body);
          }
          async::post([this] { refreshChats(false); });
        });
        if (chat.id == summary.id) chat.title = gRenameBuffer;
        gRenameId.clear();
      }
      if (ImGui::IsItemDeactivated()) gRenameId.clear();
      ImGui::PopID();
      return;
    }

    if (ImGui::InvisibleButton("##row", ImVec2(ImGui::GetContentRegionAvail().x, rowHeight))) {
      openChat(summary.id);
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* rowList = ImGui::GetWindowDrawList();
    if (active || hovered) {
      rowList->AddRectFilled(pos, ImVec2(pos.x + ImGui::GetItemRectSize().x, pos.y + rowHeight),
                             active ? palette.panelAlt : palette.border, 7.0f);
    }
    float textX = pos.x + 9.0f;
    if (summary.pinned) {
      icons::draw(rowList, icons::Icon::Pin, ImVec2(textX + 4, pos.y + rowHeight * 0.5f), 11.0f,
                  palette.textFaint);
      textX += 15.0f;
    }
    std::string title = summary.title;
    const float maxWidth = ImGui::GetItemRectSize().x - (textX - pos.x) - 10.0f;
    while (!title.empty() && ImGui::CalcTextSize(title.c_str()).x > maxWidth) {
      title.pop_back();
    }
    if (title.size() < summary.title.size() && title.size() > 1) {
      title = title.substr(0, title.size() - 1) + "...";
    }
    rowList->AddText(ImVec2(textX, pos.y + 6.0f), active ? palette.text : palette.textDim,
                     title.c_str());

    if (ImGui::BeginPopupContextItem("chatmenu")) {
      if (ImGui::MenuItem("Rename")) {
        gRenameId = summary.id;
        gRenameBuffer = summary.title;
      }
      if (ImGui::MenuItem(summary.pinned ? "Unpin" : "Pin")) {
        const std::string id = summary.id;
        async::run([this, id] {
          api_.pinChat(id);
          async::post([this] { refreshChats(false); });
        });
      }
      if (ImGui::MenuItem(summary.archived ? "Unarchive" : "Archive")) {
        const std::string id = summary.id;
        async::run([this, id] {
          api_.archiveChat(id);
          async::post([this] { refreshChats(false); });
        });
      }
      if (ImGui::MenuItem("Open in new window")) openChatInNewWindow(summary.id, false);
      if (ImGui::MenuItem("Open as floating mini window")) openChatInNewWindow(summary.id, true);
      if (ImGui::MenuItem("Duplicate")) {
        const std::string id = summary.id;
        async::run([this, id] {
          api_.cloneChat(id);
          async::post([this] { refreshChats(false); });
        });
      }
      if (ImGui::MenuItem("Share")) {
        const std::string id = summary.id;
        async::run([this, id] {
          api::Result res = api_.shareChat(id);
          async::post([this, res] {
            if (res.ok && res.data.contains("share_id")) {
              const std::string link =
                  api_.server() + "/s/" + res.data["share_id"].get<std::string>();
              platform::setClipboardText(link);
              widgets::toast(widgets::ToastLevel::Success, "Share link copied: " + link);
            } else {
              widgets::toastError("Share failed: " + api::errorMessage(res));
            }
          });
        });
      }
      if (!folders.empty() && ImGui::BeginMenu("Move to folder")) {
        for (const Json& folder : folders) {
          const std::string folderId = folder.value("id", "");
          if (ImGui::MenuItem(folder.value("name", "Folder").c_str())) {
            const std::string id = summary.id;
            async::run([this, id, folderId] {
              api_.moveChatToFolder(id, folderId);
              async::post([this] { refreshChats(false); });
            });
          }
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Delete")) gPendingDelete = summary.id;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  };

  drawPrivateSection();
  if (!pinned.empty()) {
    widgets::sectionHeader("PINNED");
    for (const ChatSummary* summary : pinned) drawRow(*summary);
  }

  std::string currentBucket;
  for (const ChatSummary* summary : loose) {
    const char* bucket = bucketFor(summary->updatedAt);
    if (bucket != currentBucket) {
      currentBucket = bucket;
      widgets::sectionHeader(bucket);
    }
    drawRow(*summary);
  }

  if (chats.empty() && !chatsLoading) {
    ImGui::Dummy(ImVec2(0, 20));
    widgets::dimText("No conversations yet");
  }
  if (chatsLoading) {
    ImGui::Dummy(ImVec2(0, 10));
    widgets::loadingRow("Loading chats...");
  }

  ImGui::Dummy(ImVec2(0, 14));
  if (ImGui::SmallButton(gShowArchived ? "Show active" : "Show archived")) {
    gShowArchived = !gShowArchived;
    if (gShowArchived) {
      async::run([this] {
        api::Result res = api_.archivedChats();
        async::post([this, res] {
          if (!res.ok || !res.data.is_array()) {
            loadChatsFromMirror(true);
            return;
          }
          chats.clear();
          for (const auto& item : res.data) {
            ChatSummary summary;
            summary.id = item.value("id", "");
            summary.title = item.value("title", "Untitled");
            summary.updatedAt = item.value("updated_at", static_cast<int64_t>(0));
            summary.archived = true;
            chats.push_back(summary);
          }
        });
      });
    } else {
      refreshChats(false);
    }
  }

  ImGui::EndChild();
  ImGui::Unindent(12.0f);

  if (!gPendingDelete.empty()) {
    if (!ImGui::IsPopupOpen("Delete chat?")) ImGui::OpenPopup("Delete chat?");
    if (widgets::confirmModal("Delete chat?", "This conversation will be removed for good.")) {
      deleteChat(gPendingDelete);
    }
    if (!ImGui::IsPopupOpen("Delete chat?")) gPendingDelete.clear();
  }

  ImGui::EndChild();
}

}  // namespace app
