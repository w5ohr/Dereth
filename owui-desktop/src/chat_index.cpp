// Semantic chat search in the app: a background pass that feeds the local
// mirror's chats to core/chatindex (embedding through Ollama), the "By
// meaning" mode of the sidebar search with its result rows, the card under
// Tools > Local index, and focusing the matched message once its chat opens.
#include <atomic>
#include <set>

#include "app.h"
#include "core/async.h"
#include "core/chatindex.h"
#include "core/common.h"
#include "core/store.h"
#include "local_embed.h"
#include "pane_common.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string indexPath() { return core::supportDir() + "/chat-index.db"; }

bool gOpened = false;
std::atomic<bool> gIndexing{false};
double gLastRun = -1.0;
double gBackoffUntil = 0.0;

std::vector<chatindex::Hit> gHits;
bool gSearching = false;
std::string gSearchError;
std::string gSearchedFor;

chatindex::Embed embedder() {
  return [](const std::vector<std::string>& inputs, std::vector<std::vector<float>>* out, std::string* error) {
    return embedTexts(inputs, out, error);
  };
}

// The messages of a mirrored ChatResponse, every branch.
std::vector<chatindex::Message> messagesOf(const Json& response) {
  std::vector<chatindex::Message> out;
  const Json chat = response.value("chat", Json::object());
  const Json history = chat.value("history", Json::object());
  const Json messages = history.value("messages", Json::object());
  if (!messages.is_object()) return out;
  for (const auto& [id, m] : messages.items()) {
    if (!m.is_object()) continue;
    chatindex::Message msg;
    msg.id = id;
    msg.role = m.value("role", "");
    const Json& content = m.contains("content") ? m["content"] : Json("");
    msg.content = content.is_string() ? content.get<std::string>() : "";
    out.push_back(msg);
  }
  return out;
}

bool ensureOpen() {
  if (!gOpened) gOpened = chatindex::open(indexPath());
  return gOpened;
}

}  // namespace

void App::tickChatIndex() {
  if (!settings().extra.value("chatIndex", true) || !store::ready() || gIndexing) return;
  const double now = core::nowMillis();
  if (gLastRun < 0.0) {   // first pass a few seconds after launch, then every minute
    gLastRun = now - 52000.0;
    return;
  }
  if (now < gBackoffUntil || now - gLastRun < 60000.0) return;
  indexChatsNow(false);
}

void App::indexChatsNow(bool full) {
  if (gIndexing) return;
  if (!ensureOpen()) {
    chatIndexStatus = "Could not open the chat index";
    return;
  }
  gIndexing = true;
  gLastRun = core::nowMillis();
  std::vector<store::ChatRow> rows = store::listChats(false);
  for (const store::ChatRow& row : store::listChats(true)) rows.push_back(row);
  if (full) chatindex::clear();
  chatIndexStatus = full ? "Rebuilding the chat index..." : chatIndexStatus;
  async::run([this, rows, full] {
    int indexed = 0, embedded = 0, examined = 0;
    bool complete = true;
    std::string error;
    std::set<std::string> live;
    const chatindex::Embed embed = embedder();
    for (const store::ChatRow& row : rows) {
      live.insert(row.id);
      if (!full && !chatindex::needsIndex(row.id, row.updatedAt)) continue;
      if (examined++ >= (full ? 100000 : 40)) {   // a slice per pass keeps the Ollama load gentle
        complete = false;
        break;
      }
      std::optional<Json> doc = store::getChat(row.id);
      if (!doc) continue;
      const int n = chatindex::indexChat(row.id, row.title, row.updatedAt, messagesOf(*doc), embed, &error);
      if (n < 0) {
        complete = false;
        break;
      }
      ++indexed;
      embedded += n;
    }
    if (error.empty()) {
      for (const std::string& id : chatindex::chatIds()) {
        if (!live.count(id)) chatindex::removeChat(id);
      }
    }
    const chatindex::Stats st = chatindex::stats();
    async::post([this, indexed, embedded, error, st, complete] {
      gIndexing = false;
      if (!error.empty()) {
        gBackoffUntil = core::nowMillis() + 5.0 * 60.0 * 1000.0;
        chatIndexStatus = "Chat index paused: " + error + " (needs the " + embedModelName() + " model at the Ollama URL)";
        return;
      }
      if (!complete) gLastRun = core::nowMillis() - 50000.0;   // more to do: come back in ten seconds
      chatIndexStatus = std::to_string(st.chats) + " chat" + (st.chats == 1 ? "" : "s") + ", " + std::to_string(st.chunks) +
                        " passage" + (st.chunks == 1 ? "" : "s") + " indexed" +
                        (indexed ? " - " + std::to_string(indexed) + " chat" + (indexed == 1 ? "" : "s") + " updated (" +
                                       std::to_string(embedded) + " passages embedded)"
                                 : "") +
                        (complete ? "" : ", more to do");
      if (indexed > 0 && chatSearchSemantic) semanticSearchDirty = true;
    });
  });
}

void App::focusMessageInChat(const std::string& messageId) {
  auto it = chat.messages.find(messageId);
  if (it == chat.messages.end()) return;
  // A hit on another branch: switch to it by walking down to a leaf.
  const std::vector<std::string> branch = chat.branch();
  if (std::find(branch.begin(), branch.end(), messageId) == branch.end()) {
    std::string cursor = messageId;
    while (true) {
      auto node = chat.messages.find(cursor);
      if (node == chat.messages.end() || node->second.childrenIds.empty()) break;
      cursor = node->second.childrenIds.back();
    }
    chat.currentId = cursor;
  }
  focusedMessageId = messageId;
  pendingScrollTo = messageId;
}

void App::drawSemanticSearchResults(float width) {
  const auto& palette = theme::palette();
  const std::string query = core::trim(chatSearch);
  if (semanticSearchDirty && !gSearching && core::nowMillis() - semanticSearchAt > 350.0) {
    semanticSearchDirty = false;
    if (!ensureOpen()) {
      gSearchError = "Could not open the chat index";
    } else {
      gSearching = true;
      gSearchError.clear();
      async::run([this, query] {
        std::string error;
        std::vector<chatindex::Hit> hits = chatindex::search(query, 25, embedder(), &error);
        async::post([this, query, hits, error] {
          gSearching = false;
          if (core::trim(chatSearch) != query) {   // typed on meanwhile
            semanticSearchDirty = true;
            return;
          }
          gHits = hits;
          gSearchError = error;
          gSearchedFor = query;
        });
      });
    }
  }

  if (gSearching && gSearchedFor != query) {
    widgets::loadingRow("Searching by meaning");
  } else if (!gSearchError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
    ImGui::PushTextWrapPos(width);
    ImGui::TextUnformatted(("Search failed: " + gSearchError).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  } else if (gHits.empty()) {
    const chatindex::Stats st = chatindex::stats();
    if (st.chunks == 0) {
      widgets::dimText("Nothing indexed yet.");
      ImGui::PushTextWrapPos(width);
      widgets::dimText("%s", chatIndexStatus.empty() ? "The index fills a few seconds after launch (Tools > Local index shows it)." : chatIndexStatus.c_str());
      ImGui::PopTextWrapPos();
    } else {
      widgets::dimText("No matches.");
    }
  } else {
    widgets::dimText("%zu passage%s, best first", gHits.size(), gHits.size() == 1 ? "" : "s");
  }
  ImGui::Dummy(ImVec2(0, 4));

  for (size_t i = 0; i < gHits.size(); ++i) {
    const chatindex::Hit& hit = gHits[i];
    ImGui::PushID(static_cast<int>(i));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetTextLineHeight();
    const float rowH = lineH * 2.0f + 16.0f;
    if (ImGui::InvisibleButton("##hit", ImVec2(width, rowH))) {
      if (hit.chatId == chat.id) {
        focusMessageInChat(hit.messageId);
        pane = Pane::Chat;
      } else {
        pendingFocusMessage = hit.messageId;
        openChat(hit.chatId);
      }
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool active = hit.chatId == chat.id;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || active) dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowH), active ? palette.panelAlt : palette.panel, 6.0f);
    dl->PushClipRect(pos, ImVec2(pos.x + width - 6.0f, pos.y + rowH), true);
    theme::pushBold();
    dl->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), palette.text, hit.title.empty() ? "Untitled" : hit.title.c_str());
    theme::pop();
    std::string snippet = hit.text;
    for (char& c : snippet) {
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    if (snippet.size() > 140) snippet = snippet.substr(0, 140) + "...";
    snippet = std::string(hit.role == "user" ? "You: " : "AI: ") + snippet;
    dl->AddText(ImVec2(pos.x + 10.0f, pos.y + 9.0f + lineH), palette.textDim, snippet.c_str());
    dl->PopClipRect();
    char score[16];
    snprintf(score, sizeof score, "%.0f%%", std::max(0.0f, hit.score) * 100.0f);
    const ImVec2 scoreSize = ImGui::CalcTextSize(score);
    dl->AddText(ImVec2(pos.x + width - scoreSize.x - 10.0f, pos.y + 6.0f), palette.textDim, score);
    if (hovered) ImGui::SetTooltip("%s", hit.text.substr(0, 600).c_str());
    ImGui::PopID();
  }
}

void App::drawChatIndexCard(float width) {
  widgets::sectionHeader("CHAT INDEX (SEARCH BY MEANING)");
  card("chatindex", [&] {
    bool on = settings().extra.value("chatIndex", true);
    if (ImGui::Checkbox("Index every chat's messages locally so the sidebar can search them by meaning", &on)) {
      settings().extra["chatIndex"] = on;
      settings().save();
    }
    const chatindex::Stats st = ensureOpen() ? chatindex::stats() : chatindex::Stats();
    widgets::dimText("%d chat%s, %d passage%s, %s on disk%s", st.chats, st.chats == 1 ? "" : "s", st.chunks, st.chunks == 1 ? "" : "s",
                     core::humanBytes(st.bytes).c_str(), gIndexing ? " - indexing..." : "");
    if (!chatIndexStatus.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 40.0f);
      widgets::dimText("%s", chatIndexStatus.c_str());
      ImGui::PopTextWrapPos();
    }
    if (ImGui::Button(gIndexing ? "Indexing..." : "Index now") && !gIndexing) {
      gBackoffUntil = 0.0;
      indexChatsNow(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild") && !gIndexing) {
      gBackoffUntil = 0.0;
      indexChatsNow(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear") && !gIndexing && ensureOpen()) {
      chatindex::clear();
      chatIndexStatus = "Cleared";
    }
    widgets::dimText("Embeddings come from %s at the Ollama URL and never leave this Mac; private chats are not indexed (their store is encrypted).",
                     embedModelName().c_str());
  }, width);
}

}  // namespace app
