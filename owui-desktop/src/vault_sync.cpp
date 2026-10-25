// Vault sync in the app: the folder setting, the sync pass (chats one way
// from the local mirror, notes both ways against the server), the FSEvents
// watch on the notes folder, the state file that remembers what was written,
// and the card under Settings > Local copy.
#include <sys/stat.h>

#include <filesystem>
#include <map>
#include <set>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/store.h"
#include "core/vault.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

namespace fs = std::filesystem;

constexpr const char* kStateFile = ".owui-vault.json";

bool gSyncing = false;
double gLastSync = -1.0;
double gWatchFiredAt = 0.0;
bool gWatchPending = false;
int gWatchToken = 0;
std::string gWatchedFolder;

std::string vaultRoot() { return core::trim(core::Settings::instance().extra.value("vaultFolder", std::string())); }
bool chatsOn() { return core::Settings::instance().extra.value("vaultChats", true); }
bool notesOn() { return core::Settings::instance().extra.value("vaultNotes", true); }

int64_t mtimeOf(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_mtime) : 0;
}

Json loadState(const std::string& root) {
  bool ok = false;
  Json j = Json::parse(core::readFile(root + "/" + kStateFile, &ok), nullptr, false);
  if (!ok || j.is_discarded() || !j.is_object()) j = Json::object();
  if (!j.contains("chats")) j["chats"] = Json::object();
  if (!j.contains("notes")) j["notes"] = Json::object();
  return j;
}

void saveState(const std::string& root, const Json& state) { core::writeFile(root + "/" + kStateFile, state.dump(1)); }

std::string noteBodyOf(const Json& note) {
  const Json data = note.value("data", Json::object());
  const Json content = data.value("content", Json());
  if (content.is_string()) return content.get<std::string>();
  if (content.is_object() && content.contains("md") && content["md"].is_string()) return content["md"];
  return "";
}

}  // namespace

void App::tickVaultSync() {
  const std::string root = vaultRoot();
  if (root.empty() || !signedIn_) {
    if (gWatchToken) {
      native::stopWatching(gWatchToken);
      gWatchToken = 0;
      gWatchedFolder.clear();
    }
    return;
  }
  // Watch the notes folder so an edit in Obsidian comes back within seconds.
  const std::string notesDir = root + "/Notes";
  if (notesOn() && gWatchedFolder != notesDir) {
    if (gWatchToken) native::stopWatching(gWatchToken);
    mkdir(root.c_str(), 0755);
    mkdir(notesDir.c_str(), 0755);
    gWatchedFolder = notesDir;
    gWatchToken = native::watchFolder(notesDir, [](const std::vector<std::string>&) {
      gWatchFiredAt = core::nowMillis();
      gWatchPending = true;
    });
  }
  const double now = core::nowMillis();
  if (gWatchPending && now - gWatchFiredAt > 2000.0) {
    gWatchPending = false;
    syncVault(false);
    return;
  }
  if (gLastSync < 0.0) {   // first pass shortly after launch, then every ten minutes
    gLastSync = now - 9.0 * 60.0 * 1000.0 - 45000.0;
    return;
  }
  if (now - gLastSync > 10.0 * 60.0 * 1000.0) syncVault(false);
}

void App::syncVault(bool force) {
  const std::string root = vaultRoot();
  if (root.empty() || gSyncing) return;
  gSyncing = true;
  gLastSync = core::nowMillis();
  vaultStatus = "Syncing...";
  const bool doChats = chatsOn(), doNotes = notesOn();
  const std::string server = api_.server();
  async::run([this, root, doChats, doNotes, server, force] {
    std::error_code ec;
    fs::create_directories(root, ec);
    Json state = loadState(root);
    int chatsWritten = 0, chatsRemoved = 0, notesWritten = 0, notesPushed = 0, notesCreated = 0, conflicts = 0;
    std::string error;

    if (doChats && store::ready()) {
      const std::string dir = root + "/Chats";
      fs::create_directories(dir, ec);
      std::vector<store::ChatRow> rows = store::listChats(false);
      for (const store::ChatRow& r : store::listChats(true)) rows.push_back(r);
      std::set<std::string> live;
      for (const store::ChatRow& row : rows) {
        live.insert(row.id);
        Json& entry = state["chats"][row.id];
        const std::string knownFile = entry.is_object() ? entry.value("file", "") : "";
        const int64_t known = entry.is_object() ? entry.value("updated", static_cast<int64_t>(0)) : 0;
        if (!force && known >= row.updatedAt && !knownFile.empty() && fs::exists(dir + "/" + knownFile, ec)) continue;
        std::optional<Json> doc = store::getChat(row.id);
        if (!doc) continue;
        const std::string file = vault::chatFileName(*doc);
        if (!knownFile.empty() && knownFile != file) fs::remove(dir + "/" + knownFile, ec);   // renamed
        if (core::writeFile(dir + "/" + file, vault::chatMarkdown(*doc, server))) {
          entry = Json{{"file", file}, {"updated", row.updatedAt}};
          ++chatsWritten;
        }
      }
      // Chats gone from the server: their files go too (only ones we wrote).
      std::vector<std::string> gone;
      for (const auto& [id, entry] : state["chats"].items()) {
        if (!live.count(id)) gone.push_back(id);
      }
      for (const std::string& id : gone) {
        const std::string file = state["chats"][id].value("file", "");
        if (!file.empty()) fs::remove(dir + "/" + file, ec);
        state["chats"].erase(id);
        ++chatsRemoved;
      }
    }

    if (doNotes) {
      const std::string dir = root + "/Notes";
      fs::create_directories(dir, ec);
      api::Result res = api_.notes();
      if (!res.ok) {
        error = "notes: " + api::errorMessage(res);
      } else {
        std::map<std::string, Json> byId;
        for (const auto& n : res.data.is_array() ? res.data : Json::array()) {
          if (n.is_object() && n.contains("id") && n["id"].is_string()) byId[n["id"]] = n;
        }
        std::set<std::string> knownFiles;
        // Server -> files, and files edited here -> server.
        for (auto& [id, note] : byId) {
          Json& entry = state["notes"][id];
          const std::string knownFile = entry.is_object() ? entry.value("file", "") : "";
          const int64_t knownUpdated = entry.is_object() ? entry.value("updated", static_cast<int64_t>(0)) : 0;
          const int64_t knownMtime = entry.is_object() ? entry.value("mtime", static_cast<int64_t>(0)) : 0;
          const int64_t serverUpdated = note.value("updated_at", static_cast<int64_t>(0));
          const std::string path = knownFile.empty() ? "" : dir + "/" + knownFile;
          const bool fileExists = !path.empty() && fs::exists(path, ec);
          const bool editedHere = fileExists && mtimeOf(path) > knownMtime + 1;
          const bool changedOnServer = serverUpdated > knownUpdated;
          if (editedHere && !force) {
            bool ok = false;
            const vault::NoteFile parsed = vault::parseNoteFile(core::readFile(path, &ok), knownFile);
            if (ok && parsed.ok) {
              if (changedOnServer) {
                // Both sides moved: the server's text wins, the local edit is kept beside it.
                core::writeFile(dir + "/" + vault::safeName(parsed.title) + " (conflict " + core::formatTime(core::nowSeconds(), "%Y-%m-%d %H%M") + ").md", core::readFile(path));
                ++conflicts;
              } else {
                Json body{{"title", parsed.title}, {"data", Json{{"content", Json{{"md", parsed.body}}}}}, {"meta", note.value("meta", Json::object())}};
                api::Result up = api_.updateNote(id, body);
                if (up.ok) {
                  ++notesPushed;
                  note["title"] = parsed.title;
                  note["data"] = body["data"];
                  note["updated_at"] = up.data.is_object() ? up.data.value("updated_at", serverUpdated) : serverUpdated;
                } else {
                  error = "note push: " + api::errorMessage(up);
                }
              }
            }
          }
          const std::string file = vault::noteFileName(note);
          const int64_t updatedNow = note.value("updated_at", static_cast<int64_t>(0));
          const bool rewrite = force || !fileExists || changedOnServer || file != knownFile || editedHere;
          if (rewrite) {
            if (!knownFile.empty() && knownFile != file) fs::remove(dir + "/" + knownFile, ec);
            if (core::writeFile(dir + "/" + file, vault::noteMarkdown(note))) ++notesWritten;
          }
          entry = Json{{"file", file}, {"updated", updatedNow}, {"mtime", mtimeOf(dir + "/" + file)}};
          knownFiles.insert(file);
        }
        // Notes deleted on the server lose their files; new .md files here become notes.
        std::vector<std::string> gone;
        for (const auto& [id, entry] : state["notes"].items()) {
          if (!byId.count(id)) gone.push_back(id);
        }
        for (const std::string& id : gone) {
          const std::string file = state["notes"][id].value("file", "");
          if (!file.empty()) fs::remove(dir + "/" + file, ec);
          state["notes"].erase(id);
        }
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
          if (!entry.is_regular_file(ec)) continue;
          const std::string name = entry.path().filename().string();
          if (name.empty() || name[0] == '.' || !core::endsWith(name, ".md") || knownFiles.count(name)) continue;
          if (name.find("(conflict ") != std::string::npos) continue;
          bool ok = false;
          const std::string text = core::readFile(entry.path().string(), &ok);
          if (!ok) continue;
          const vault::NoteFile parsed = vault::parseNoteFile(text, name);
          if (!parsed.id.empty() && byId.count(parsed.id)) continue;   // a stale copy of a note we track under another name
          Json body{{"title", parsed.title}, {"data", Json{{"content", Json{{"md", parsed.body}}}}}, {"meta", Json::object()}};
          api::Result created = api_.createNote(body);
          if (!created.ok || !created.data.is_object()) {
            error = "note create: " + api::errorMessage(created);
            continue;
          }
          Json note = created.data;
          if (!note.contains("data")) note["data"] = body["data"];
          const std::string file = vault::noteFileName(note);
          if (file != name) fs::remove(entry.path(), ec);
          core::writeFile(dir + "/" + file, vault::noteMarkdown(note));
          state["notes"][note.contains("id") && note["id"].is_string() ? note["id"].get<std::string>() : std::string("?")] = Json{{"file", file}, {"updated", note.value("updated_at", static_cast<int64_t>(0))}, {"mtime", mtimeOf(dir + "/" + file)}};
          ++notesCreated;
        }
      }
    }
    saveState(root, state);
    async::post([this, chatsWritten, chatsRemoved, notesWritten, notesPushed, notesCreated, conflicts, error] {
      gSyncing = false;
      std::string s = "Synced " + core::formatTime(core::nowSeconds(), "%H:%M") + " - " + std::to_string(chatsWritten) + " chat file" +
                      (chatsWritten == 1 ? "" : "s") + " written";
      if (chatsRemoved) s += ", " + std::to_string(chatsRemoved) + " removed";
      s += "; notes: " + std::to_string(notesWritten) + " written, " + std::to_string(notesPushed) + " pushed to the server, " +
           std::to_string(notesCreated) + " created from new files";
      if (conflicts) s += ", " + std::to_string(conflicts) + " conflict cop" + (conflicts == 1 ? "y" : "ies");
      if (!error.empty()) s += " - " + error;
      vaultStatus = s;
      if (notesPushed || notesCreated) {
        widgets::toast(widgets::ToastLevel::Success, std::to_string(notesPushed + notesCreated) + " note" + (notesPushed + notesCreated == 1 ? "" : "s") + " synced from the vault");
        noteList = Json::array();   // the Notes pane reloads
      }
    });
  });
}

void App::drawVaultSection(float width) {
  card("vault", [&] {
    ImGui::TextUnformatted("Mirror chats and notes as Markdown files - point this at a folder inside your Obsidian vault.");
    widgets::dimText("Chats: Chats/<title> (<id>).md with front matter, written whenever a chat changes. Notes: Notes/<title>.md both ways - edit one here or in Obsidian and it syncs; a new .md in Notes becomes a note.");
    ImGui::Dummy(ImVec2(0, 4));
    std::string root = vaultRoot();
    ImGui::SetNextItemWidth(std::max(200.0f, width - 300.0f));
    if (ImGui::InputTextWithHint("##vaultroot", "/Users/you/Obsidian/MyVault/Open WebUI", &root)) {
      settings().extra["vaultFolder"] = root;
      settings().save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose...")) {
      const std::vector<std::string> picked = native::chooseFolder();
      if (!picked.empty()) {
        settings().extra["vaultFolder"] = picked.front();
        settings().save();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reveal") && !vaultRoot().empty()) platform::revealInFinder(vaultRoot());
    bool chats = chatsOn(), notes = notesOn();
    if (ImGui::Checkbox("Chats (one way, from the local copy)", &chats)) {
      settings().extra["vaultChats"] = chats;
      settings().save();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Notes (both ways)", &notes)) {
      settings().extra["vaultNotes"] = notes;
      settings().save();
    }
    if (!vaultStatus.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 40.0f);
      widgets::dimText("%s", vaultStatus.c_str());
      ImGui::PopTextWrapPos();
    }
    widgets::beginDisabled(gSyncing || vaultRoot().empty());
    if (widgets::primaryButton(gSyncing ? "Syncing..." : "Sync now", ImVec2(120, 0))) syncVault(false);
    ImGui::SameLine();
    if (ImGui::Button("Rewrite everything", ImVec2(160, 0))) syncVault(true);
    widgets::endDisabled(gSyncing || vaultRoot().empty());
    widgets::dimText("Runs after every chat sync, every ten minutes, and a few seconds after a note file changes. Private chats are never written.");
  }, width);
}

}  // namespace app
