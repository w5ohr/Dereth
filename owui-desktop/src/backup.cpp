// Backup and import (Tools > Backup): everything the app holds, in one zip
// - every chat from the server as JSON, the settings, the usage log, the
// local mirror, private chats (still encrypted), plugins, redaction map and
// apply history - and the other way round: a ChatGPT or Claude export
// becomes chats on the server.
#include "app.h"

#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <filesystem>

#include "core/async.h"
#include "core/common.h"
#include "core/importers.h"
#include "core/store.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

namespace fs = std::filesystem;

struct BackupState {
  bool exporting = false;
  bool importing = false;
  std::string status;        // last outcome, shown in the pane
  int importDone = 0, importTotal = 0;
  bool includePrivate = false;
  bool includeMirror = true;
};
BackupState g;

std::string stamp() {
  std::time_t t = std::time(nullptr);
  std::tm local{};
  localtime_r(&t, &local);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H%M", &local);
  return buf;
}

std::string safeName(std::string s) {
  for (char& c : s) {
    if (c == '/' || c == ':' || c == '\\' || c == '\n') c = '-';
  }
  if (s.size() > 60) s = s.substr(0, 60);
  return s;
}

}  // namespace

void App::exportEverything() {
  const std::string path = platform::saveFile("open-webui-backup-" + stamp() + ".zip");
  if (path.empty()) return;
  g.exporting = true;
  g.status = "Exporting...";
  const bool withPrivate = g.includePrivate, withMirror = g.includeMirror;
  const std::string support = core::supportDir();
  async::run([this, path, withPrivate, withMirror, support] {
    std::error_code ec;
    const std::string work = "/tmp/owui-backup-" + std::to_string(getpid()) + "-" + std::to_string(std::time(nullptr));
    fs::create_directories(work + "/chats", ec);
    // Every chat, one JSON each: the id list from the server, bodies from the
    // mirror when it has them (saves a round-trip), else fetched.
    int chats = 0, failed = 0;
    api::Result all = api_.chatListAll();
    std::vector<std::string> ids;
    if (all.ok) {
      for (const std::string& line : core::split(all.raw, '\n')) {
        Json j = Json::parse(line, nullptr, false);
        if (!j.is_discarded() && j.is_object() && j.contains("id")) ids.push_back(j["id"]);
      }
    }
    if (ids.empty()) {
      for (int page = 1; page < 200; ++page) {
        api::Result list = api_.chatList(page);
        if (!list.ok || !list.data.is_array() || list.data.empty()) break;
        for (const auto& item : list.data) {
          if (item.contains("id")) ids.push_back(item["id"]);
        }
      }
    }
    for (const std::string& id : ids) {
      Json body;
      if (auto cached = store::getChat(id)) body = *cached;
      if (body.is_null()) {
        api::Result res = api_.chat(id);
        if (res.ok) body = res.data;
      }
      if (body.is_null()) {
        ++failed;
        continue;
      }
      const std::string title = safeName(body.value("title", "chat"));
      core::writeFile(work + "/chats/" + title + "-" + id.substr(0, 8) + ".json", body.dump(2));
      ++chats;
    }
    // App data.
    auto copyIf = [&](const std::string& rel, bool on) {
      if (!on) return;
      const std::string from = support + "/" + rel;
      if (fs::exists(from, ec)) fs::copy(from, work + "/" + rel, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    };
    copyIf("settings.json", true);
    copyIf("usage.jsonl", true);
    copyIf("redactions.json", true);
    copyIf("apply-history.json", true);
    copyIf("plugins", true);
    copyIf("private", withPrivate);
    for (const auto& entry : fs::directory_iterator(support, ec)) {
      const std::string name = entry.path().filename().string();
      if (withMirror && core::startsWith(name, "mirror-") && name.find(".db") != std::string::npos && name.find("-wal") == std::string::npos && name.find("-shm") == std::string::npos) {
        fs::copy(entry.path(), work + "/" + name, fs::copy_options::overwrite_existing, ec);
      }
    }
    core::writeFile(work + "/README.txt",
                    "Open WebUI Desktop backup, " + stamp() + "\n\nchats/            every chat as the server's JSON (import with Tools > Backup, or POST /api/v1/chats/new)\n"
                    "settings.json     app settings (tokens are in the keychain, not here)\nusage.jsonl       the usage log\n"
                    "private/          private chats, still encrypted with this Mac's key\nmirror-*.db       the offline mirror\nplugins/          JavaScript plugins\n");
    // Zip with ditto (keeps metadata, no external tool).
    unlink(path.c_str());
    native::ProcessResult zip = native::runProcess({"/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", work, path}, work, 300, false);
    fs::remove_all(work, ec);
    const bool ok = zip.exitCode == 0 && fs::exists(path, ec);
    const int64_t size = ok ? static_cast<int64_t>(fs::file_size(path, ec)) : 0;
    async::post([this, ok, chats, failed, path, size, zip] {
      g.exporting = false;
      if (!ok) {
        g.status = "Export failed: " + core::trim(zip.output);
        widgets::toastError(g.status);
        return;
      }
      g.status = "Exported " + std::to_string(chats) + " chats" + (failed ? " (" + std::to_string(failed) + " could not be read)" : "") +
                 " to " + core::baseName(path) + " (" + core::humanBytes(size) + ")";
      widgets::toast(widgets::ToastLevel::Success, g.status);
      platform::revealInFinder(path);
    });
  });
}

// A ChatGPT / Claude export, or a folder of this app's own chat JSON files.
void App::importChats() {
  const std::vector<std::string> picked = platform::openFiles(true);
  if (picked.empty()) return;
  const std::string model = selectedModels.empty() ? "imported" : selectedModels.front();
  g.importing = true;
  g.importDone = 0;
  g.importTotal = 0;
  g.status = "Reading...";
  async::run([this, picked, model] {
    std::vector<Json> bodies;
    std::string sources;
    for (const std::string& file : picked) {
      bool ok = false;
      const std::string raw = core::readFile(file, &ok);
      if (!ok) continue;
      Json doc = Json::parse(raw, nullptr, false);
      if (doc.is_discarded()) continue;
      const importers::Source source = importers::detect(doc);
      if (source != importers::Source::Unknown) {
        for (const importers::Imported& imp : importers::convert(doc, source, model)) bodies.push_back(imp.chat);
        if (sources.find(importers::sourceName(source)) == std::string::npos) sources += std::string(sources.empty() ? "" : ", ") + importers::sourceName(source);
      } else if (doc.is_object() && doc.contains("chat") && doc["chat"].is_object()) {
        bodies.push_back(doc["chat"]);   // this app's own backup file
        if (sources.find("backup") == std::string::npos) sources += std::string(sources.empty() ? "" : ", ") + "backup";
      } else if (doc.is_object() && doc.contains("history")) {
        bodies.push_back(doc);           // a bare chat body
      }
    }
    async::post([this, bodies] {
      g.importTotal = static_cast<int>(bodies.size());
      if (bodies.empty()) {
        g.importing = false;
        g.status = "Nothing recognised: expected a ChatGPT or Claude conversations.json, or chat JSON from a backup";
        widgets::toastInfo(g.status);
      }
    });
    int done = 0, failed = 0;
    for (const Json& body : bodies) {
      Json clean = body;
      clean.erase("id");
      api::Result res = api_.createChat(clean);
      if (res.ok) {
        ++done;
        if (res.data.is_object()) store::putChat(res.data);
      } else {
        ++failed;
      }
      async::post([this, done] { g.importDone = done; });
    }
    if (!bodies.empty()) {
      async::post([this, done, failed, sources] {
        g.importing = false;
        g.status = "Imported " + std::to_string(done) + (done == 1 ? " chat" : " chats") + " from " + sources +
                   (failed ? " (" + std::to_string(failed) + " failed)" : "");
        widgets::toast(widgets::ToastLevel::Success, g.status);
        refreshChats();
      });
    }
  });
}

void App::drawBackupSection(float width) {
  (void)width;
  widgets::sectionHeader("BACKUP");
  widgets::dimText("One zip with every chat as JSON, the settings, usage log, plugins and the offline mirror.\n"
                   "Sign-in tokens stay in the keychain and are never written out.");
  ImGui::Checkbox("Include the offline mirror database", &g.includeMirror);
  ImGui::Checkbox("Include private chats (encrypted; readable only with this Mac's key)", &g.includePrivate);
  widgets::beginDisabled(g.exporting);
  if (widgets::primaryButton(g.exporting ? "Exporting..." : "Export everything...", ImVec2(200, 0))) exportEverything();
  widgets::endDisabled(g.exporting);

  ImGui::Dummy(ImVec2(0, 10));
  widgets::sectionHeader("IMPORT");
  widgets::dimText("ChatGPT (Settings > Data controls > Export: conversations.json) and Claude (Settings > Privacy >\n"
                   "Export data: conversations.json) exports become chats on this server, tagged imported-chatgpt /\n"
                   "imported-claude; the shown branch of a ChatGPT tree is kept. Chat JSON from a backup imports too.");
  widgets::beginDisabled(g.importing);
  if (widgets::primaryButton(g.importing ? "Importing..." : "Import chats...", ImVec2(200, 0))) importChats();
  widgets::endDisabled(g.importing);
  if (g.importing && g.importTotal > 0) {
    ImGui::SameLine();
    widgets::dimText("%d / %d", g.importDone, g.importTotal);
  }
  if (!g.status.empty()) {
    ImGui::Dummy(ImVec2(0, 6));
    widgets::dimText("%s", g.status.c_str());
  }
}

}  // namespace app
