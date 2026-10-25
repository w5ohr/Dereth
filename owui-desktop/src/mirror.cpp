// The local-first mirror in the app: periodic sync with the server, the
// offline chat list, and the settings section. The database itself is
// core/store.*; the read/write hooks live where chats are loaded and saved
// (app.cpp) and in the sidebar's search.
#include "app.h"

#include <map>
#include <set>
#include <vector>

#include "core/async.h"
#include "core/common.h"
#include "core/store.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

// /api/v1/chats/all streams one JSON object per chat rather than an array;
// this reads a body that is an array, newline-delimited objects, or objects
// simply concatenated.
std::vector<Json> parseJsonStream(const std::string& raw) {
  std::vector<Json> out;
  Json whole = Json::parse(raw, nullptr, false);
  if (!whole.is_discarded()) {
    if (whole.is_array()) {
      for (auto& item : whole) out.push_back(item);
    } else if (whole.is_object()) {
      out.push_back(whole);
    }
    return out;
  }
  size_t start = std::string::npos;
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = 0; i < raw.size(); ++i) {
    const char ch = raw[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') inString = false;
      continue;
    }
    if (ch == '"') {
      inString = true;
    } else if (ch == '{') {
      if (depth++ == 0) start = i;
    } else if (ch == '}') {
      if (--depth == 0 && start != std::string::npos) {
        Json item = Json::parse(raw.substr(start, i - start + 1), nullptr, false);
        if (!item.is_discarded()) out.push_back(item);
        start = std::string::npos;
      }
    }
  }
  return out;
}

}  // namespace

void App::loadChatsFromMirror(bool archived) {
  chats.clear();
  for (const store::ChatRow& row : store::listChats(archived)) {
    ChatSummary summary;
    summary.id = row.id;
    summary.title = row.title.empty() ? "Untitled" : row.title;
    summary.folderId = row.folderId;
    summary.updatedAt = row.updatedAt;
    summary.pinned = row.pinned;
    summary.archived = row.archived;
    chats.push_back(summary);
  }
  offlineList = true;
}

void App::syncMirror(bool force) {
  if (!signedIn_ || mirrorSyncing || !store::ready()) return;
  const double now = core::nowMillis();
  if (!force && lastMirrorSync > 0.0 && now - lastMirrorSync < 10.0 * 60.0 * 1000.0) return;
  lastMirrorSync = now;
  mirrorSyncing = true;
  mirrorStatus = "Syncing...";

  async::run([this] {
    // Edits made offline go first, so the pull below can't overwrite them.
    int pushed = 0;
    int failed = 0;
    for (auto& [id, row] : store::dirtyChats()) {
      Json body = row.value("chat", Json::object());
      api::Result current = api_.chat(id);
      if (!current.ok) {
        if (current.status == 0) {   // still unreachable; try again later
          ++failed;
          continue;
        }
      } else {
        // Changed on the server since the edit: keep the newer title; the
        // server merges message histories itself.
        const int64_t serverUpdated = current.data.value("updated_at", static_cast<int64_t>(0));
        if (serverUpdated > store::dirtyBase(id)) {
          const std::string title = current.data.value("title", "");
          if (!title.empty()) body["title"] = title;
        }
      }
      api::Result res = api_.updateChat(id, body);
      if (res.ok) {
        store::putChat(res.data);
        ++pushed;
      } else {
        ++failed;
      }
    }

    // Pull whatever is newer than the mirror, and drop what the server no
    // longer has (never a row that still carries an unsent edit).
    api::Result all = api_.chatListAll();
    std::vector<Json> items;
    if (all.ok) items = parseJsonStream(all.raw);
    // Some servers paginate instead; walk the pages and fetch each chat.
    if (all.ok && items.empty()) {
      for (int page = 1; page < 200; ++page) {
        api::Result list = api_.chatList(page);
        if (!list.ok || !list.data.is_array() || list.data.empty()) break;
        for (const auto& item : list.data) items.push_back(item);
      }
    }
    int pulled = 0;
    int removed = 0;
    if (all.ok) {
      std::map<std::string, store::ChatRow> known;
      for (bool archived : {false, true}) {
        for (const store::ChatRow& row : store::listChats(archived)) known[row.id] = row;
      }
      std::set<std::string> seen;
      for (const auto& item : items) {
        if (!item.is_object()) continue;
        const std::string id = item.value("id", "");
        if (id.empty()) continue;
        seen.insert(id);
        auto k = known.find(id);
        if (k != known.end() && k->second.dirty) continue;
        const int64_t updated = item.value("updated_at", static_cast<int64_t>(0));
        if (k != known.end() && k->second.updatedAt >= updated) continue;
        if (item.contains("chat") && item["chat"].is_object() && item.contains("updated_at")) {
          store::putChat(item);
        } else {
          api::Result one = api_.chat(id);
          if (!one.ok) continue;
          store::putChat(one.data);
        }
        ++pulled;
      }
      for (const auto& [id, row] : known) {
        if (!seen.count(id) && !row.dirty) {
          store::removeChat(id);
          ++removed;
        }
      }
      store::setMeta("lastSync", std::to_string(core::nowSeconds()));
    }

    const bool ok = all.ok;
    async::post([this, pushed, failed, pulled, removed, ok] {
      mirrorSyncing = false;
      if (!ok) {
        mirrorStatus = "Sync failed - server unreachable";
        return;
      }
      mirrorStatus = "Synced " + core::formatTime(core::nowSeconds(), "%H:%M") + " - " + std::to_string(pulled) +
                     " pulled, " + std::to_string(pushed) + " pushed" +
                     (removed ? ", " + std::to_string(removed) + " removed" : "") +
                     (failed ? ", " + std::to_string(failed) + " still pending" : "");
      if (pushed > 0) {
        widgets::toast(widgets::ToastLevel::Success,
                       std::to_string(pushed) + " offline edit" + (pushed == 1 ? "" : "s") + " synced");
        refreshChats();
      }
      // The vault follows the local copy.
      if (!core::trim(settings().extra.value("vaultFolder", std::string())).empty()) syncVault(false);
    });
  });
}

void App::drawMirrorSection(float width) {
  const store::Stats stats = store::stats();
  card("mirror", [&] {
    ImGui::TextUnformatted("Every chat is copied into a local database so it can be read and searched "
                           "while the server is away; edits made offline are sent when it returns.");
    ImGui::Dummy(ImVec2(0, 4));
    field("Chats cached", std::to_string(stats.chats));
    field("Unsent edits", std::to_string(stats.dirty));
    field("Size", core::humanBytes(stats.bytes));
    field("Last full sync", stats.lastSync ? core::relativeTime(stats.lastSync) : "never");
    field("Database", stats.path);
    if (!mirrorStatus.empty()) widgets::dimText("%s", mirrorStatus.c_str());
    ImGui::Dummy(ImVec2(0, 4));
    widgets::beginDisabled(mirrorSyncing);
    if (widgets::primaryButton(mirrorSyncing ? "Syncing..." : "Sync now", ImVec2(120, 0))) syncMirror(true);
    widgets::endDisabled(mirrorSyncing);
    ImGui::SameLine();
    if (ImGui::Button("Rebuild local copy", ImVec2(160, 0))) {
      store::clear();
      syncMirror(true);
    }
  }, width);
}

}  // namespace app
