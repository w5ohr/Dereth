#include <algorithm>
// Settings: appearance, chat behaviour, voice, account and connection.
#include "core/async.h"
#include "core/common.h"
#include "core/keychain.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string gProfileName;
std::string gOldPassword;
std::string gNewPassword;
std::string gApiKey;
bool gApiKeyLoaded = false;
bool gFontsDirty = false;

}  // namespace

void App::drawSettingsPane() {
  core::Settings& cfg = settings();
  if (gProfileName.empty()) gProfileName = user_.value("name", "");

  paneHeader("Settings", "This app and your account", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);
    if (widgets::primaryButton("Save", ImVec2(100, 30))) {
      cfg.save();
      widgets::toast(widgets::ToastLevel::Success, "Settings saved");
    }
  });

  paneBody("##settings");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;

  widgets::sectionHeader("APPEARANCE");
  card("appearance", [&] {
    const char* themes[] = {"dark", "light", "system", "midnight"};
    int themeIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(themes); ++i) {
      if (cfg.theme == themes[i]) themeIndex = i;
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Theme", &themeIndex, themes, IM_ARRAYSIZE(themes))) {
      cfg.theme = themes[themeIndex];
      applyTheme();
      cfg.save();
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("Text size", &cfg.fontSize, 11.0f, 22.0f, "%.0f px")) {
      gFontsDirty = true;
    }
    if (gFontsDirty && !ImGui::IsAnyItemActive()) {
      gFontsDirty = false;
      ImGui::GetStyle().FontSizeBase = cfg.fontSize;
      cfg.save();
    }
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("Sidebar width", &cfg.sidebarWidth, 220.0f, 420.0f, "%.0f px");
    ImGui::Checkbox("Show message timestamps", &cfg.showTimestamps);
    ImGui::Checkbox("Render markdown", &cfg.renderMarkdown);
  }, width);

  widgets::sectionHeader("ACCESSIBILITY");
  drawAccessibilitySection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("THEME EDITOR");
  drawThemeEditorSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("READING MODE");
  drawReadingModeSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("LSP CONTEXT");
  card("lsp", [&] { drawLspSection(width); }, width);

  widgets::sectionHeader("LAYOUTS");
  card("layouts", [&] { drawLayoutsSection(width); }, width);

  widgets::sectionHeader("CHAT");
  card("chat", [&] {
    ImGui::Checkbox("Stream responses", &cfg.streamResponses);
    ImGui::Checkbox("Enter sends the message", &cfg.sendOnEnter);
    bool localDocs = cfg.extra.value("localDocuments", true);
    if (ImGui::Checkbox("Read attached documents on this Mac (PDF, Word, PowerPoint, Excel, text) instead of uploading them", &localDocs)) {
      cfg.extra["localDocuments"] = localDocs;
      cfg.save();
    }
    widgets::dimText("The text goes with the prompt, page by page, and replies can cite pages. Private chats always read locally.");
    bool memProps = cfg.extra.value("memoryProposals", true);
    if (ImGui::Checkbox("After a reply, suggest facts about you worth remembering (saved to Memories only when you accept)", &memProps)) {
      cfg.extra["memoryProposals"] = memProps;
      cfg.save();
    }
    bool artifactAutoOpen = cfg.extra.value("artifactAutoOpen", true);
    if (ImGui::Checkbox("Open the artifact pane when a reply contains HTML or SVG", &artifactAutoOpen)) {
      cfg.extra["artifactAutoOpen"] = artifactAutoOpen;
      cfg.save();
    }
    bool nativeComposer = cfg.extra.value("nativeComposer", true);
    if (ImGui::Checkbox("Native text field in the composer (IME, dictation, spellcheck, emoji)",
                        &nativeComposer)) {
      cfg.extra["nativeComposer"] = nativeComposer;
      cfg.save();
    }
    ImGui::Checkbox("Ask the server to name new chats", &cfg.autoTitle);
    ImGui::Checkbox("Notify when a reply finishes", &cfg.notifyOnCompletion);
    ImGui::Checkbox("Read replies aloud", &cfg.speakReplies);
    bool virtualOn = cfg.extra.value("virtualTranscript", true);
    if (ImGui::Checkbox("Lay out only the visible part of long chats (virtualised transcript)", &virtualOn)) {
      cfg.extra["virtualTranscript"] = virtualOn;
      cfg.save();
    }
    drawLogprobSettings();
    widgets::dimText("Live socket: %s", socketConnected() ? "connected" : "offline");
  }, width);

  widgets::sectionHeader("LOCAL COPY");
  drawMirrorSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("VAULT SYNC (OBSIDIAN)");
  drawVaultSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("WARMUP AND RESIDENCY");
  drawWarmupSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("LOW POWER");
  drawLowPowerSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("EDITOR");
  drawEditorSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("VOICE AND READ-ALOUD");
  drawTtsSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("SMART FOLDERS AND TAGS");
  drawSmartFoldersSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("KEYBOARD SHORTCUTS");
  drawShortcutsSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("GETTING STARTED");
  drawOnboardingSection(width);
  ImGui::Dummy(ImVec2(0, 8));

  widgets::sectionHeader("ACCOUNT");
  card("account", [&] {
    field("Name", user_.value("name", ""));
    field("Email", user_.value("email", ""));
    field("Role", user_.value("role", ""));

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##profilename", "Display name", &gProfileName);
    ImGui::SameLine();
    if (ImGui::Button("Update profile", ImVec2(140, 0))) {
      const std::string name = gProfileName;
      const std::string avatar = user_.value("profile_image_url", "");
      async::run([this, name, avatar] {
        api::Result res = api_.updateProfile(name, avatar);
        async::post([this, res, name] {
          if (res.ok) {
            user_["name"] = name;
            widgets::toast(widgets::ToastLevel::Success, "Profile updated");
          } else {
            widgets::toastError(api::errorMessage(res));
          }
        });
      });
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##oldpw", "Current password", &gOldPassword,
                             ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##newpw", "New password", &gNewPassword,
                             ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button("Change password", ImVec2(150, 0)) && !gNewPassword.empty()) {
      const std::string oldPassword = gOldPassword;
      const std::string newPassword = gNewPassword;
      gOldPassword.clear();
      gNewPassword.clear();
      async::run([this, oldPassword, newPassword] {
        api::Result res = api_.updatePassword(oldPassword, newPassword);
        async::post([res] {
          if (res.ok) {
            widgets::toast(widgets::ToastLevel::Success, "Password changed");
          } else {
            widgets::toastError(api::errorMessage(res));
          }
        });
      });
    }

    ImGui::Dummy(ImVec2(0, 6));
    if (!gApiKeyLoaded) {
      gApiKeyLoaded = true;
      async::run([this] {
        api::Result res = api_.getApiKey();
        async::post([res] {
          if (res.ok) gApiKey = res.data.value("api_key", "");
        });
      });
    }
    field("API key", gApiKey.empty() ? "(none)" : gApiKey.substr(0, 12) + "...");
    if (ImGui::Button("Create API key", ImVec2(150, 0))) {
      async::run([this] {
        api::Result res = api_.createApiKey();
        async::post([res] {
          if (res.ok) {
            gApiKey = res.data.value("api_key", "");
            widgets::toast(widgets::ToastLevel::Success, "API key created");
          }
        });
      });
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy key", ImVec2(110, 0)) && !gApiKey.empty()) {
      platform::setClipboardText(gApiKey);
      widgets::toast(widgets::ToastLevel::Success, "API key copied");
    }
    ImGui::SameLine();
    if (ImGui::Button("Revoke key", ImVec2(120, 0))) {
      async::run([this] {
        api_.deleteApiKey();
        async::post([] {
          gApiKey.clear();
          widgets::toastInfo("API key revoked");
        });
      });
    }
  }, width);

  widgets::sectionHeader("MAC INTEGRATION");
  card("mac", [&] {
    bool hotkey = cfg.extra.value("globalHotkey", true);
    if (ImGui::Checkbox("Option-Space opens Quick Ask from any app", &hotkey)) {
      cfg.extra["globalHotkey"] = hotkey;
      cfg.save();
      if (hotkey) {
        if (!native::registerHotkey(49, false, true, false, false)) {
          widgets::toastError("Option-Space is already taken by another app");
        }
      } else {
        native::unregisterHotkey();
      }
    }
    bool captureHotkey = cfg.extra.value("captureHotkey", true);
    if (ImGui::Checkbox("Control-Option-S captures part of the screen into the chat from any app", &captureHotkey)) {
      cfg.extra["captureHotkey"] = captureHotkey;
      cfg.save();
      if (captureHotkey) {
        if (!native::registerCaptureHotkey(1, false, true, true, false)) {
          widgets::toastError("Control-Option-S is already taken by another app");
        }
      } else {
        native::unregisterCaptureHotkey();
      }
    }
    bool redactScan = cfg.extra.value("redactScan", true);
    if (ImGui::Checkbox("Check prompts for secrets and personal data before they are sent", &redactScan)) {
      cfg.extra["redactScan"] = redactScan;
      cfg.save();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("API keys, tokens, private keys, passwords, card numbers, SSNs, IBANs, emails, phone\n"
                        "numbers and IP addresses are shown for review and masked with placeholders; the\n"
                        "real values stay on this Mac and the chat shows them.");
    }
    bool captureOcr = cfg.extra.value("captureOcr", true);
    if (ImGui::Checkbox("Put the text found in a screenshot into the composer as well", &captureOcr)) {
      cfg.extra["captureOcr"] = captureOcr;
      cfg.save();
    }
    bool menuBar = cfg.extra.value("menuBarExtra", true);
    const bool menuBarOnly = cfg.extra.value("menuBarOnly", false);
    widgets::beginDisabled(menuBarOnly);
    if (ImGui::Checkbox("Show in the menu bar", &menuBar)) {
      cfg.extra["menuBarExtra"] = menuBar;
      cfg.save();
      native::setStatusItemVisible(menuBar);
    }
    widgets::endDisabled(menuBarOnly);
    drawMenuBarOnlySection(width);
    widgets::dimText("Links: owui://ask?prompt=...  owui://chat/<id>  owui://quick");
    widgets::dimText("Services: select text anywhere > Services > Ask Open WebUI");
    if (ImGui::Button("Clear Spotlight index", ImVec2(200, 0))) {
      native::clearSpotlightIndex();
      widgets::toastInfo("Spotlight entries removed");
    }
  }, width);

  widgets::sectionHeader("AUTO-ROUTING");
  card("routing", [&] {
    bool route = cfg.extra.value("autoRoute", false);
    if (ImGui::Checkbox("Send short, code-free questions to a smaller model", &route)) {
      cfg.extra["autoRoute"] = route;
      cfg.save();
    }
    std::string small = cfg.extra.value("autoRouteSmallModel", "");
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("Small model", small.empty() ? "(choose)" : small.c_str())) {
      for (const ModelInfo& info : models) {
        if (ImGui::Selectable(info.name.c_str(), info.id == small)) {
          cfg.extra["autoRouteSmallModel"] = info.id;
          cfg.save();
        }
      }
      ImGui::EndCombo();
    }
    int threshold = cfg.extra.value("autoRouteTokenThreshold", 80);
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderInt("Token threshold", &threshold, 20, 400)) {
      cfg.extra["autoRouteTokenThreshold"] = threshold;
      cfg.save();
    }
    widgets::dimText("Anything with code, attachments, a long prompt or a long chat stays on your chosen model.");
  }, width);

  widgets::sectionHeader("SCHEDULED PROMPTS");
  card("schedules", [&] {
    static std::string newName, newPrompt;
    static int newHour = 8, newMinute = 0;
    static bool newWeekdays = true;
    Json& list = cfg.extra["schedules"];
    if (!list.is_array()) list = Json::array();
    int removeIndex = -1;
    for (size_t i = 0; i < list.size(); ++i) {
      ScheduledPrompt schedule = ScheduledPrompt::fromJson(list[i]);
      ImGui::PushID(static_cast<int>(i));
      bool enabled = schedule.enabled;
      if (ImGui::Checkbox("##on", &enabled)) {
        schedule.enabled = enabled;
        list[i] = schedule.toJson();
        cfg.save();
      }
      ImGui::SameLine();
      ImGui::Text("%02d:%02d %s  %s", schedule.hour, schedule.minute,
                  schedule.weekdaysOnly ? "weekdays" : "daily",
                  schedule.name.empty() ? schedule.prompt.substr(0, 60).c_str() : schedule.name.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
      ImGui::PopID();
    }
    if (removeIndex >= 0) {
      list.erase(list.begin() + removeIndex);
      cfg.save();
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##schedname", "Name", &newName);
    ImGui::SetNextItemWidth(width - 60.0f);
    ImGui::InputTextMultiline("##schedprompt", &newPrompt, ImVec2(width - 60.0f, 60));
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Hour", &newHour);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Minute", &newMinute);
    ImGui::SameLine();
    ImGui::Checkbox("Weekdays only", &newWeekdays);
    newHour = std::clamp(newHour, 0, 23);
    newMinute = std::clamp(newMinute, 0, 59);
    if (ImGui::Button("Add schedule", ImVec2(160, 0)) && !core::trim(newPrompt).empty()) {
      ScheduledPrompt schedule;
      schedule.id = core::uuid();
      schedule.name = newName;
      schedule.prompt = newPrompt;
      schedule.model = selectedModels.empty() ? "" : selectedModels.front();
      schedule.hour = newHour;
      schedule.minute = newMinute;
      schedule.weekdaysOnly = newWeekdays;
      list.push_back(schedule.toJson());
      cfg.save();
      newName.clear();
      newPrompt.clear();
    }
    widgets::dimText("Runs while the app is open, once per day, in a new chat with the model selected when added.");
  }, width);

  widgets::sectionHeader("PRESETS");
  card("presets", [&] {
    Json& list = cfg.extra["presets"];
    if (!list.is_array()) list = Json::array();
    if (list.empty()) widgets::dimText("Save one from the Presets button in a chat's header.");
    int removeIndex = -1;
    for (size_t i = 0; i < list.size(); ++i) {
      Preset preset = Preset::fromJson(list[i]);
      ImGui::PushID(static_cast<int>(i));
      ImGui::Text("%s  -  %s", preset.name.c_str(), core::join(preset.models, ", ").c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Apply")) applyPreset(preset);
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) removeIndex = static_cast<int>(i);
      ImGui::PopID();
    }
    if (removeIndex >= 0) {
      list.erase(list.begin() + removeIndex);
      cfg.save();
    }
  }, width);

  widgets::sectionHeader("SERVER PROFILES");
  card("profiles", [&] {
    static std::string newName, newUrl, newEmail;
    std::vector<ServerProfile> list = profiles();
    int removeIndex = -1;
    for (size_t i = 0; i < list.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      const bool current = list[i].url == api_.server();
      ImGui::Text("%s%s  %s", current ? "* " : "  ", list[i].name.c_str(), list[i].url.c_str());
      ImGui::SameLine();
      if (!current && ImGui::SmallButton("Switch")) switchProfile(list[i]);
      ImGui::SameLine();
      if (list.size() > 1 && ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
      ImGui::PopID();
    }
    if (removeIndex >= 0) {
      list.erase(list.begin() + removeIndex);
      saveProfiles(list);
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##pname", "Name", &newName);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320);
    ImGui::InputTextWithHint("##purl", "http://host:3000", &newUrl);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##pemail", "Email", &newEmail);
    ImGui::SameLine();
    if (ImGui::Button("Add") && !core::trim(newUrl).empty()) {
      list.push_back({newName.empty() ? newUrl : newName, core::trim(newUrl), newEmail});
      saveProfiles(list);
      newName.clear();
      newUrl.clear();
      newEmail.clear();
    }
    widgets::dimText("Each server keeps its own sign-in in the keychain.");
  }, width);

  widgets::sectionHeader("DIRECT BACKENDS");
  card("backends", [&] { drawBackendsSection(width); }, width);

  widgets::sectionHeader("BACKEND POOL");
  card("pool", [&] { drawBackendPoolSection(width); }, width);

  widgets::sectionHeader("CONNECTION");
  card("connection", [&] {
    field("Server", api_.server());
    field("Version", serverConfig_.value("version", ""));
    ImGui::SetNextItemWidth(320);
    ImGui::InputTextWithHint("##ollamaurl", "Ollama URL", &cfg.ollamaUrl);
    ImGui::SetNextItemWidth(320);
    ImGui::InputTextWithHint("##comfyurl", "ComfyUI URL", &cfg.comfyUrl);
    widgets::dimText("Used by the direct-backend fallbacks when the server is unreachable.");
    ImGui::Dummy(ImVec2(0, 6));
    if (widgets::dangerButton("Sign out", ImVec2(150, 0))) signOut();
    ImGui::SameLine();
    if (ImGui::Button("Clear image cache", ImVec2(180, 0))) {
      images::clear();
      widgets::toastInfo("Image cache cleared");
    }
  }, width);

  widgets::sectionHeader("CRASH REPORTS");
  card("crashes", [&] { drawCrashSection(width); }, width);

  widgets::sectionHeader("ABOUT");
  card("about", [&] {
    field("Open WebUI Desktop", platform::appVersion());
    field("Build", std::string(__DATE__) + " " + __TIME__);
    field("Renderer", "Dear ImGui " IMGUI_VERSION " on Metal");
    widgets::dimText("A native C++ client for Open WebUI.");
  }, width);

  paneEnd();
}

}  // namespace app
