// Settings: appearance, chat behaviour, voice, account and connection.
#include "core/async.h"
#include "core/common.h"
#include "core/keychain.h"
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
Json gVoices = Json::array();
bool gVoicesLoaded = false;
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
    const char* themes[] = {"dark", "light"};
    int themeIndex = cfg.theme == "light" ? 1 : 0;
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Theme", &themeIndex, themes, IM_ARRAYSIZE(themes))) {
      cfg.theme = themes[themeIndex];
      theme::apply(cfg.theme);
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

  widgets::sectionHeader("CHAT");
  card("chat", [&] {
    ImGui::Checkbox("Stream responses", &cfg.streamResponses);
    ImGui::Checkbox("Enter sends the message", &cfg.sendOnEnter);
    ImGui::Checkbox("Ask the server to name new chats", &cfg.autoTitle);
    ImGui::Checkbox("Notify when a reply finishes", &cfg.notifyOnCompletion);
    ImGui::Checkbox("Read replies aloud", &cfg.speakReplies);
    widgets::dimText("Live socket: %s", socketConnected() ? "connected" : "offline");
  }, width);

  widgets::sectionHeader("VOICE");
  card("voice", [&] {
    if (!gVoicesLoaded) {
      gVoicesLoaded = true;
      async::run([this] {
        api::Result res = api_.voices();
        async::post([res] {
          if (res.ok) {
            gVoices = res.data.is_array() ? res.data : res.data.value("voices", Json::array());
          }
        });
      });
    }
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("Voice", cfg.ttsVoice.empty() ? "(server default)"
                                                        : cfg.ttsVoice.c_str())) {
      if (ImGui::Selectable("(server default)", cfg.ttsVoice.empty())) cfg.ttsVoice.clear();
      for (const auto& voice : gVoices) {
        const std::string id = voice.is_string() ? voice.get<std::string>()
                                                 : voice.value("id", voice.value("name", ""));
        if (id.empty()) continue;
        if (ImGui::Selectable(id.c_str(), id == cfg.ttsVoice)) cfg.ttsVoice = id;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Test", ImVec2(90, 0))) {
      speak("This is the Open WebUI desktop client.");
    }
    if (platform::audioPlaying()) {
      ImGui::SameLine();
      if (ImGui::Button("Stop", ImVec2(80, 0))) platform::stopAudio();
    }
  }, width);

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
