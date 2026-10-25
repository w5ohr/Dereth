// Onboarding in the app (#145): the first-run tour (a card with a spotlight
// on the part of the window it talks about), the "what's new" sheet shown
// once per update, and the Settings card to bring either back.
#include <algorithm>

#include "app.h"
#include "core/common.h"
#include "core/onboarding.h"
#include "core/settings.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

bool gChecked = false;
int gStep = 0;
std::vector<onboarding::Release> gPending;

onboarding::State state() { return onboarding::fromJson(core::Settings::instance().extra); }
void saveState(const onboarding::State& s) {
  core::Settings::instance().extra["onboarding"] = onboarding::toJson(s);
  core::Settings::instance().save();
}

}  // namespace

void App::tickOnboarding() {
  if (gChecked || !signedIn_) return;
  gChecked = true;
  const onboarding::State s = state();
  if (!s.tourDone) {
    gStep = s.tourStep;
    showTour = true;
    return;
  }
  gPending = onboarding::pendingReleases(s.lastSeenVersion, platform::appVersion());
  if (!gPending.empty()) showWhatsNew = true;
}

void App::startTour() {
  gStep = 0;
  showTour = true;
  pane = Pane::Chat;
}

void App::showWhatsNewSheet(bool everything) {
  gPending = everything ? onboarding::releases() : onboarding::pendingReleases(state().lastSeenVersion, platform::appVersion());
  if (gPending.empty()) gPending = onboarding::releases();
  showWhatsNew = true;
}

void App::drawTour() {
  if (!showTour) return;
  const auto& steps = onboarding::tourSteps();
  gStep = std::clamp(gStep, 0, static_cast<int>(steps.size()) - 1);
  const onboarding::TourStep& step = steps[gStep];
  const auto& palette = theme::palette();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 origin = vp->WorkPos, size = vp->WorkSize;

  // Spotlight: a rectangle around the part of the window the step talks about.
  const float railW = 56.0f;
  const float sidebarW = sidebarVisible ? settings().sidebarWidth : 0.0f;
  ImVec2 a, b;
  bool spot = true;
  if (step.anchor == "rail") a = ImVec2(origin.x, origin.y), b = ImVec2(origin.x + railW, origin.y + size.y);
  else if (step.anchor == "sidebar") a = ImVec2(origin.x + railW, origin.y), b = ImVec2(origin.x + railW + std::max(sidebarW, 200.0f), origin.y + size.y);
  else if (step.anchor == "header") a = ImVec2(origin.x + railW + sidebarW, origin.y), b = ImVec2(origin.x + size.x, origin.y + 64.0f);
  else if (step.anchor == "composer") a = ImVec2(origin.x + railW + sidebarW + 40.0f, origin.y + size.y - 150.0f), b = ImVec2(origin.x + size.x - 40.0f, origin.y + size.y - 12.0f);
  else if (step.anchor == "tools") a = ImVec2(origin.x, origin.y + size.y - 320.0f), b = ImVec2(origin.x + railW, origin.y + size.y - 120.0f);
  else if (step.anchor == "settings") a = ImVec2(origin.x, origin.y + size.y - 120.0f), b = ImVec2(origin.x + railW, origin.y + size.y - 40.0f);
  else spot = false;
  // The modal dims everything; the spotlight outline goes on top of it.
  if (spot) ImGui::GetForegroundDrawList()->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f), palette.accent, 8.0f, 3.0f);

  // The card: beside the spotlight, else centred.
  const ImVec2 cardSize(440.0f, 0.0f);
  ImVec2 pos(origin.x + (size.x - cardSize.x) * 0.5f, origin.y + size.y * 0.3f);
  if (step.anchor == "rail" || step.anchor == "tools" || step.anchor == "settings") pos = ImVec2(origin.x + railW + 40.0f, spot ? std::max(origin.y + 80.0f, a.y - 60.0f) : pos.y);
  else if (step.anchor == "sidebar") pos = ImVec2(b.x + 40.0f, origin.y + 120.0f);
  else if (step.anchor == "composer") pos = ImVec2(a.x + 40.0f, a.y - 230.0f);
  else if (step.anchor == "header") pos = ImVec2(a.x + 40.0f, b.y + 30.0f);
  pos.x = std::min(pos.x, origin.x + size.x - cardSize.x - 20.0f);
  pos.y = std::max(pos.y, origin.y + 20.0f);
  ImGui::OpenPopup("##tour");
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::toVec4(palette.panel));
  ImGui::PushStyleColor(ImGuiCol_Border, theme::toVec4(palette.accent));
  const bool open = ImGui::BeginPopupModal("##tour", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
  if (!open) {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return;
  }
  widgets::dimText("Step %d of %zu", gStep + 1, steps.size());
  theme::pushScaled(1.25f);
  theme::pushBold();
  ImGui::TextUnformatted(step.title.c_str());
  theme::pop();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::PushTextWrapPos(cardSize.x - 40.0f);
  ImGui::TextUnformatted(step.body.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, 10));
  auto finish = [&] {
    onboarding::State s = state();
    s.tourDone = true;
    s.tourStep = 0;
    saveState(s);
    showTour = false;
    ImGui::CloseCurrentPopup();
    gPending = onboarding::pendingReleases(s.lastSeenVersion, platform::appVersion());
    if (!gPending.empty()) showWhatsNew = true;
  };
  if (ImGui::Button("Skip tour")) finish();
  ImGui::SameLine();
  if (gStep > 0 && ImGui::Button("Back")) --gStep;
  ImGui::SameLine();
  const bool last = gStep + 1 >= static_cast<int>(steps.size());
  if (widgets::primaryButton(last ? "Finish" : "Next", ImVec2(110, 0))) {
    if (last) finish();
    else {
      ++gStep;
      onboarding::State s = state();
      s.tourStep = gStep;
      saveState(s);
    }
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) finish();
  ImGui::EndPopup();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void App::drawWhatsNew() {
  if (!showWhatsNew) return;
  const std::string title = gPending.empty() ? "What's new" : "What's new in " + gPending.front().version;
  ImGui::OpenPopup("What's new");
  ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("What's new", &showWhatsNew)) return;
  auto dismiss = [&] {
    onboarding::State s = state();
    s.lastSeenVersion = platform::appVersion();
    saveState(s);
    showWhatsNew = false;
    ImGui::CloseCurrentPopup();
  };
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) dismiss();
  theme::pushBold();
  ImGui::TextUnformatted(title.c_str());
  theme::pop();
  ImGui::BeginChild("##releases", ImVec2(0, -36.0f));
  for (const onboarding::Release& r : gPending) {
    theme::pushBold();
    ImGui::Text("Version %s", r.version.c_str());
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("%s", r.date.c_str());
    for (const std::string& item : r.items) {
      ImGui::Bullet();
      ImGui::SameLine();
      ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
      ImGui::TextUnformatted(item.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 8));
  }
  ImGui::EndChild();
  if (widgets::primaryButton("Got it", ImVec2(110, 0))) dismiss();
  ImGui::SameLine();
  if (ImGui::Button("Copy")) platform::setClipboardText(onboarding::whatsNewText(gPending));
  ImGui::SameLine();
  if (ImGui::Button("Take the tour")) {
    dismiss();
    startTour();
  }
  ImGui::EndPopup();
}

void App::drawOnboardingSection(float width) {
  card("onboarding", [&] {
    const onboarding::State s = state();
    widgets::dimText("Version %s - tour %s - release notes last seen: %s", platform::appVersion().c_str(), s.tourDone ? "taken" : "not taken yet", s.lastSeenVersion.empty() ? "never" : s.lastSeenVersion.c_str());
    if (ImGui::Button("Take the tour")) startTour();
    ImGui::SameLine();
    if (ImGui::Button("What's new")) showWhatsNewSheet(true);
    ImGui::SameLine();
    if (ImGui::Button("Shortcut cheat sheet")) showCheatSheet = true;
  }, width);
}

}  // namespace app
