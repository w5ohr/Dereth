// Crash reporter in the app (#130): the handlers are installed at start-up
// (main.mm); on the next launch, reports newer than the last one seen - ours
// and macOS's .ips for this app - open a sheet with the reason and the top of
// the stack, View / Copy / Save as / Show in Finder / Report on GitHub. Nothing
// leaves the Mac unless the user clicks Report, which only opens a prefilled
// new-issue page (version, signal, function names; no activity, no home paths).
#include <cstdlib>

#include "app.h"
#include "core/common.h"
#include "core/crashreport.h"
#include "core/settings.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<crashreport::Report> gNew;   // shown in the sheet
size_t gSelected = 0;
bool gShowFull = false;
bool gChecked = false;
std::string gFullText;
int gFrames = 0;
bool gFromHistory = false;   // opened from Settings, not raised at launch

std::string home() { return getenv("HOME") ? getenv("HOME") : ""; }
std::string issueRepo() { return core::Settings::instance().extra.value("crashIssueRepo", std::string("w5ohr/Dereth")); }

}  // namespace

std::string App::crashFolder() const { return core::supportDir() + "/crashes"; }
std::string App::diagnosticsFolder() const { return home() + "/Library/Logs/DiagnosticReports"; }

std::vector<crashreport::Report> App::crashReports() const { return crashreport::list(crashFolder(), diagnosticsFolder(), "Open WebUI"); }

void App::showCrashReports(const std::vector<crashreport::Report>& reports) {
  gFromHistory = false;
  gNew = reports;
  gSelected = 0;
  gShowFull = false;
  gFullText = gNew.empty() ? "" : gNew.front().text;
  showCrashSheet = !gNew.empty();
}

void App::tickCrashReporter() {
  ++gFrames;
  // Test hook: crash on purpose a few frames in (OWUI_CRASH_TEST=segv|abort|throw).
  if (gFrames == 30) {
    if (const char* kind = getenv("OWUI_CRASH_TEST")) {
      crashreport::note("OWUI_CRASH_TEST: crashing on purpose");
      const std::string k = kind;
      if (k == "segv") {
        volatile int* p = nullptr;
        *p = 1;
      } else if (k == "abort") {
        abort();
      } else if (k == "throw") {
        throw std::runtime_error("OWUI_CRASH_TEST asked for an uncaught exception");
      }
    }
  }
  if (gChecked) return;
  gChecked = true;
  Json& state = settings().extra["crashReports"];
  if (!state.is_object()) state = Json::object();
  if (!state.contains("seen")) {   // first launch with the reporter: don't surface old history
    state["seen"] = core::nowSeconds();
    settings().save();
    return;
  }
  const int64_t seen = state.value("seen", static_cast<int64_t>(0));
  const auto fresh = crashreport::newerThan(crashReports(), seen);
  if (!fresh.empty()) {
    core::debugLog("crash reporter: %zu new report(s) since %lld", fresh.size(), static_cast<long long>(seen));
    showCrashReports(fresh);
  }
}

void App::markCrashesSeen() {
  int64_t newest = settings().extra["crashReports"].value("seen", static_cast<int64_t>(0));
  for (const auto& r : gNew) newest = std::max(newest, r.time);
  settings().extra["crashReports"]["seen"] = newest;
  settings().save();
}

void App::drawCrashSheet() {
  if (!showCrashSheet || gNew.empty()) return;
  const auto& palette = theme::palette();
  const char* title = gFromHistory ? "Crash report###crashsheet" : "Open WebUI quit unexpectedly###crashsheet";
  ImGui::OpenPopup("###crashsheet");
  ImGui::SetNextWindowSize(ImVec2(820, gShowFull ? 680 : 520), ImGuiCond_Always);
  if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) return;
  if (gSelected >= gNew.size()) gSelected = 0;
  const crashreport::Report& r = gNew[gSelected];
  ImGui::PushTextWrapPos(790);
  ImGui::TextUnformatted(gFromHistory ? "A crash recorded earlier." : gNew.size() == 1 ? "The app crashed last time. Here is what it recorded." : ("The app crashed " + std::to_string(gNew.size()) + " times since you last looked. Pick one:").c_str());
  ImGui::PopTextWrapPos();
  if (gNew.size() > 1) {
    ImGui::SetNextItemWidth(780);
    if (ImGui::BeginCombo("##crashpick", crashreport::summary(r).c_str())) {
      for (size_t i = 0; i < gNew.size(); ++i) {
        if (ImGui::Selectable((crashreport::summary(gNew[i]) + "##" + std::to_string(i)).c_str(), i == gSelected)) {
          gSelected = i;
          gFullText = gNew[i].text;
        }
      }
      ImGui::EndCombo();
    }
  }
  ImGui::Separator();
  theme::pushBold();
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
  ImGui::PushTextWrapPos(790);
  ImGui::TextUnformatted(crashreport::summary(r).c_str());   // long C++ names wrap instead of running off the sheet
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  theme::pop();
  if (!r.reason.empty()) widgets::dimText("%s", r.reason.c_str());
  widgets::dimText("Version %s - %s - %s", r.version.empty() ? "?" : r.version.c_str(), r.source == "macos" ? "macOS crash report" : "the app's own report",
                   crashreport::redactHome(r.path, home()).c_str());
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted("Top of the stack");
  theme::pushMono();
  ImGui::BeginChild("##frames", ImVec2(790, gShowFull ? 120 : 200), ImGuiChildFlags_Borders);
  for (size_t i = 0; i < r.frames.size() && i < 16; ++i) ImGui::TextUnformatted((std::to_string(i) + "  " + r.frames[i]).c_str());
  if (r.frames.empty()) widgets::dimText("(no frames recorded)");
  ImGui::EndChild();
  theme::pop();
  if (!r.notes.empty()) {
    widgets::dimText("Last activity before the crash: %s", r.notes.back().substr(0, 140).c_str());
  }
  if (gShowFull) {
    theme::pushMono();
    ImGui::InputTextMultiline("##fullcrash", &gFullText, ImVec2(790, 220), ImGuiInputTextFlags_ReadOnly);
    theme::pop();
  }
  ImGui::Dummy(ImVec2(0, 4));
  if (ImGui::Button(gShowFull ? "Hide full report" : "View full report")) gShowFull = !gShowFull;
  ImGui::SameLine();
  if (ImGui::Button("Copy report")) {
    platform::setClipboardText(r.text);
    widgets::toast(widgets::ToastLevel::Success, "Crash report copied");
  }
  ImGui::SameLine();
  if (ImGui::Button("Save as...")) {
    const std::string path = platform::saveFile(r.source == "macos" ? "Open WebUI crash.ips" : "Open WebUI crash.txt");
    if (!path.empty() && core::writeFile(path, r.text)) widgets::toast(widgets::ToastLevel::Success, "Saved " + path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Show in Finder")) platform::revealInFinder(r.path);
  ImGui::SameLine();
  if (ImGui::Button("Report on GitHub...")) platform::openUrl(crashreport::issueUrl(issueRepo(), r, home()));
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Opens a new-issue page on github.com/%s in your browser with the version, signal and function names filled in.\nNothing is sent until you submit it there; recent activity and home-folder paths are left out.", issueRepo().c_str());
  }
  ImGui::SameLine(700);
  if (widgets::primaryButton(gFromHistory ? "Close" : "Dismiss", ImVec2(90, 0))) {
    if (!gFromHistory) markCrashesSeen();
    showCrashSheet = false;
    gNew.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::drawCrashSection(float width) {
  const auto reports = crashReports();
  ImGui::PushTextWrapPos(width - 20.0f);
  widgets::dimText("When the app crashes it writes a report (what failed, the stack, the last few things it was doing) to %s; macOS keeps its own in ~/Library/Logs/DiagnosticReports. On the next launch new ones open in a sheet. Nothing is sent anywhere unless you choose Report on GitHub.",
                   crashreport::redactHome(crashFolder(), home()).c_str());
  ImGui::PopTextWrapPos();
  if (reports.empty()) {
    widgets::dimText("No crash reports.");
  } else {
    for (size_t i = 0; i < reports.size() && i < 12; ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::SmallButton("View")) {
        showCrashReports({reports[i]});
        gFromHistory = true;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(crashreport::summary(reports[i]).c_str());
      ImGui::PopID();
    }
  }
  if (ImGui::SmallButton("Open the crash folder")) platform::revealInFinder(crashFolder());
}

}  // namespace app
