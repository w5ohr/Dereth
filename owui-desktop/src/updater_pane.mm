// Auto-update in the app (#129): daily checks of the appcast, a Settings card
// (check, release notes, install and relaunch, skip, roll back), and a sheet
// after relaunching into a new version. Installing only accepts an item whose
// SHA-256 matches, over HTTPS (or localhost), holding this same app; the
// version replaced is kept so one click puts it back.
#import <Cocoa/Cocoa.h>

#include <spawn.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <filesystem>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/settings.h"
#include "core/updater.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

extern char** environ;
namespace fs = std::filesystem;

namespace app {
namespace {

const char* kDefaultFeed = "https://github.com/w5ohr/Dereth/releases/latest/download/appcast.xml";
bool gChecking = false, gInstalling = false, gStarted = false;
int gFrames = 0;
std::string gStatus;
bool gHasItem = false;
updater::Item gItem;
std::string gFeedEdit;
Json gJustUpdated;   // {from, to, notes} after relaunching into a new version
bool gShowUpdated = false;

std::string updatesDir() { return core::supportDir() + "/updates"; }
std::string backupsDir() { return updatesDir() + "/previous"; }
std::string bundlePath() { return [[[NSBundle mainBundle] bundlePath] UTF8String]; }
std::string bundleId() {
  NSString* s = [[NSBundle mainBundle] bundleIdentifier];
  return s ? [s UTF8String] : "com.davidcochran.owui-desktop";
}
std::string osVersion() {
  char buf[64] = {0};
  size_t size = sizeof(buf);
  if (sysctlbyname("kern.osproductversion", buf, &size, nullptr, 0) != 0) return "";
  return buf;
}
Json& prefs() {
  Json& p = core::Settings::instance().extra["updates"];
  if (!p.is_object()) p = Json::object();
  return p;
}
std::string feed() { return prefs().value("feed", std::string(kDefaultFeed)); }
std::vector<std::string> skipped() {
  std::vector<std::string> out;
  for (const auto& v : prefs().value("skipped", Json::array())) {
    if (v.is_string()) out.push_back(v);
  }
  return out;
}

// Starts the (new or restored) app a moment after this one quits, with the same environment.
void relaunch() {
  const std::string exe = [[[NSBundle mainBundle] executablePath] UTF8String];
  const std::string cmd = "sleep 1; exec \"" + exe + "\"";
  std::vector<std::string> store{"/bin/sh", "-c", cmd};
  std::vector<char*> argv;
  for (auto& a : store) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid;
  posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv.data(), environ);
  [NSApp terminate:nil];
}

}  // namespace

void App::checkForUpdates(bool manual) {
  if (gChecking) return;
  const std::string url = feed();
  if (url.empty()) {
    gStatus = "No update feed set.";
    return;
  }
  if (url.rfind("https://", 0) != 0 && url.rfind("http://127.0.0.1", 0) != 0 && url.rfind("http://localhost", 0) != 0) {
    gStatus = "The update feed must use HTTPS.";
    return;
  }
  gChecking = true;
  gStatus = "Checking " + url + "...";
  async::run([this, url, manual] {
    net::Request req;
    req.url = url;
    req.timeoutSec = 20;
    req.connectTimeoutSec = 8;
    const net::Response res = net::perform(req);
    std::string error;
    std::vector<updater::Item> items;
    if (!res.error.empty() || res.status != 200) error = res.error.empty() ? "the feed answered HTTP " + std::to_string(res.status) : res.error;
    else items = updater::parseAppcast(res.body, &error);
    const int at = error.empty() ? updater::pick(items, platform::appVersion(), osVersion(), skipped()) : -1;
    std::string refused;
    for (const auto& it : items) {
      if (updater::compare(it.version, platform::appVersion()) > 0 && !updater::problem(it, osVersion()).empty()) refused = it.version + " (" + updater::problem(it, osVersion()) + ")";
    }
    async::post([this, error, items, at, refused, manual] {
      gChecking = false;
      prefs()["lastCheck"] = core::nowSeconds();
      settings().save();
      if (!error.empty()) {
        gStatus = "Could not check: " + error;
        gHasItem = false;
        return;
      }
      if (at < 0) {
        gHasItem = false;
        gStatus = "Up to date (" + platform::appVersion() + ")" + (refused.empty() ? "" : " - not offered: " + refused);
        return;
      }
      gItem = items[static_cast<size_t>(at)];
      gHasItem = true;
      gStatus = "Version " + gItem.version + " is available.";
      if (!manual) widgets::toastInfo("Open WebUI " + gItem.version + " is available - Settings > Updates");
    });
  });
}

void App::installUpdate() {
  if (!gHasItem || gInstalling) return;
  const updater::Item item = gItem;
  const std::string target = bundlePath();
  if (target.find("/AppTranslocation/") != std::string::npos) {
    gStatus = "The app is running from a temporary, read-only location: move it to Applications first.";
    return;
  }
  if (access(fs::path(target).parent_path().c_str(), W_OK) != 0) {
    gStatus = "No permission to replace the app in " + fs::path(target).parent_path().string() + ".";
    return;
  }
  gInstalling = true;
  gStatus = "Downloading " + item.version + "...";
  const std::string from = platform::appVersion();
  const std::string id = bundleId();
  async::run([item, target, from, id] {
    std::error_code ec;
    fs::create_directories(updatesDir(), ec);
    const std::string dmg = updatesDir() + "/Open-WebUI-" + item.version + ".dmg";
    net::Request req;
    req.url = item.url;
    req.timeoutSec = 600;
    const net::Response res = net::download(req, dmg);
    std::string error;
    if (!res.error.empty() || res.status != 200) error = "download failed: " + (res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error);
    else if (item.length > 0 && static_cast<long long>(fs::file_size(dmg, ec)) != item.length) error = "the download is " + std::to_string(fs::file_size(dmg, ec)) + " bytes, the feed said " + std::to_string(item.length);
    else if (updater::sha256File(dmg) != item.sha256) error = "the download's SHA-256 does not match the feed - not installing it";
    updater::Result r;
    if (error.empty()) {
      async::post([] { gStatus = "Installing..."; });
      r = updater::installFromDmg(dmg, id, item.version, target, backupsDir());
      if (!r.ok) error = r.error;
    }
    fs::remove(dmg, ec);
    if (error.empty()) {
      const Json note{{"from", from}, {"to", item.version}, {"notes", item.notes}, {"at", core::nowSeconds()}};
      core::writeFile(updatesDir() + "/updated.json", note.dump());
    }
    core::debugLog("update %s -> %s: %s%s", from.c_str(), item.version.c_str(), error.empty() ? "installed" : error.c_str(), r.log.empty() ? "" : ("\n" + r.log).c_str());
    async::post([error] {
      gInstalling = false;
      if (!error.empty()) {
        gStatus = "Not updated: " + error;
        return;
      }
      gStatus = "Installed - relaunching...";
      relaunch();
    });
  });
}

void App::rollbackUpdate() {
  const std::string target = bundlePath();
  const std::string to = updater::backupVersion(backupsDir());
  const updater::Result r = updater::rollback(target, backupsDir());
  if (!r.ok) {
    gStatus = "Could not roll back: " + r.error;
    return;
  }
  core::writeFile(updatesDir() + "/updated.json", Json{{"from", platform::appVersion()}, {"to", to}, {"notes", ""}, {"rolledBack", true}, {"at", core::nowSeconds()}}.dump());
  relaunch();
}

void App::tickUpdater() {
  ++gFrames;
  if (!gStarted) {
    gStarted = true;
    bool ok = false;
    const std::string raw = core::readFile(updatesDir() + "/updated.json", &ok);
    if (ok) {
      gJustUpdated = Json::parse(raw, nullptr, false);
      std::error_code ec;
      fs::remove(updatesDir() + "/updated.json", ec);
      if (gJustUpdated.is_object() && gJustUpdated.value("to", "") == platform::appVersion()) gShowUpdated = true;
    }
  }
  // A quiet daily check a little after start-up.
  if (gFrames == 120 && prefs().value("autoCheck", true) && core::nowSeconds() - prefs().value("lastCheck", static_cast<int64_t>(0)) > 86400) checkForUpdates(false);
}

void App::drawUpdatedSheet() {
  if (!gShowUpdated) return;
  const bool rolledBack = gJustUpdated.value("rolledBack", false);
  const std::string title = std::string(rolledBack ? "Rolled back to " : "Updated to ") + platform::appVersion() + "###updatedsheet";
  ImGui::OpenPopup("###updatedsheet");
  ImGui::SetNextWindowSize(ImVec2(620, rolledBack ? 200 : 440), ImGuiCond_Always);
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize)) return;
  ImGui::Text("%s %s to %s.", rolledBack ? "Rolled back from" : "Updated from", gJustUpdated.value("from", "?").c_str(), platform::appVersion().c_str());
  const std::string notes = gJustUpdated.value("notes", "");
  if (!notes.empty()) {
    ImGui::Separator();
    ImGui::BeginChild("##updnotes", ImVec2(590, 280), ImGuiChildFlags_None);
    md::Options o;
    o.wrapWidth = 570;
    md::render("updnotes", notes, o);
    ImGui::EndChild();
  }
  const std::string previous = updater::backupVersion(backupsDir());
  if (!previous.empty() && !rolledBack) {
    if (ImGui::Button(("Roll back to " + previous).c_str())) {
      gShowUpdated = false;
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      rollbackUpdate();
      return;
    }
    ImGui::SameLine();
  }
  ImGui::SetCursorPosX(620 - 110);
  if (widgets::primaryButton("OK", ImVec2(90, 0))) {
    gShowUpdated = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::drawUpdatesSection(float width) {
  const auto& palette = theme::palette();
  if (gFeedEdit.empty()) gFeedEdit = feed();
  field("This version", platform::appVersion() + "  (" + bundlePath() + ")");
  ImGui::SetNextItemWidth(width - 160.0f);
  if (ImGui::InputText("Feed", &gFeedEdit, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
    prefs()["feed"] = core::trim(gFeedEdit);
    settings().save();
  }
  bool autoCheck = prefs().value("autoCheck", true);
  if (ImGui::Checkbox("Check once a day", &autoCheck)) {
    prefs()["autoCheck"] = autoCheck;
    settings().save();
  }
  ImGui::SameLine();
  widgets::beginDisabled(gChecking || gInstalling);
  if (ImGui::Button(gChecking ? "Checking..." : "Check now")) checkForUpdates(true);
  widgets::endDisabled(gChecking || gInstalling);
  const int64_t last = prefs().value("lastCheck", static_cast<int64_t>(0));
  ImGui::SameLine();
  widgets::dimText("%s", last ? ("last checked " + core::relativeTime(last)).c_str() : "never checked");
  if (!gStatus.empty()) {
    ImGui::PushTextWrapPos(width - 20.0f);
    ImGui::TextColored(theme::toVec4(gStatus.rfind("Not updated", 0) == 0 || gStatus.rfind("Could not", 0) == 0 ? palette.danger : gHasItem ? palette.accent : palette.textDim), "%s", gStatus.c_str());
    ImGui::PopTextWrapPos();
  }
  if (gHasItem) {
    theme::pushBold();
    ImGui::TextUnformatted((gItem.title.empty() ? "Version " + gItem.version : gItem.title).c_str());
    theme::pop();
    widgets::dimText("%s%s%s", gItem.pubDate.c_str(), gItem.length > 0 ? (" - " + core::humanBytes(gItem.length)).c_str() : "", (" - sha256 " + gItem.sha256.substr(0, 12) + "...").c_str());
    if (!gItem.notes.empty()) {
      ImGui::BeginChild("##relnotes", ImVec2(width - 20.0f, 180), ImGuiChildFlags_Borders);
      md::Options o;
      o.wrapWidth = width - 50.0f;
      md::render("relnotes", gItem.notes, o);
      ImGui::EndChild();
    }
    widgets::beginDisabled(gInstalling);
    if (widgets::primaryButton(gInstalling ? "Installing..." : "Install and relaunch", ImVec2(180, 0))) installUpdate();
    widgets::endDisabled(gInstalling);
    ImGui::SameLine();
    if (ImGui::Button("Skip this version")) {
      Json list = prefs().value("skipped", Json::array());
      list.push_back(gItem.version);
      prefs()["skipped"] = list;
      settings().save();
      gHasItem = false;
      gStatus = "Skipped " + gItem.version + ".";
    }
  }
  const std::string previous = updater::backupVersion(backupsDir());
  if (!previous.empty()) {
    if (ImGui::Button(("Roll back to " + previous).c_str())) rollbackUpdate();
    ImGui::SameLine();
    widgets::dimText("the version this one replaced, kept in the support folder");
  }
  ImGui::PushTextWrapPos(width - 20.0f);
  widgets::dimText("An update installs only if its SHA-256 matches the feed, it comes over HTTPS (or from this Mac), and it is this same app. Builds are ad hoc signed until Developer ID signing, so the feed's host is trusted.");
  ImGui::PopTextWrapPos();
}

}  // namespace app
