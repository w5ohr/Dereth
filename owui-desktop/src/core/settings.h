// Persisted app preferences (~/Library/Application Support/Open WebUI Desktop/settings.json).
// The session token is NOT here — that lives in the keychain.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace core {

struct Settings {
  // Reachable at home and over Tailscale from anywhere.
  std::string serverUrl = "http://davids-macbook-pro.tailba9313.ts.net:3000";
  std::string email;
  bool rememberSession = true;

  // Direct-backend fallbacks, usable without an Open WebUI account.
  std::string ollamaUrl = "http://davids-macbook-pro.tailba9313.ts.net:11434";
  std::string comfyUrl = "http://davids-macbook-pro.tailba9313.ts.net:8188";

  std::string theme = "dark";          // dark | light | midnight
  float fontSize = 15.0f;
  float uiScale = 1.0f;
  float sidebarWidth = 280.0f;
  bool showTimestamps = true;
  bool renderMarkdown = true;
  bool autoTitle = true;               // ask the server to name new chats
  bool autoFollowUps = true;
  bool streamResponses = true;
  bool sendOnEnter = true;
  bool notifyOnCompletion = true;
  bool speakReplies = false;
  std::string ttsVoice;

  std::vector<std::string> selectedModels;
  std::vector<std::string> recentServers;

  nlohmann::json extra = nlohmann::json::object();

  void load();
  void save() const;
  static Settings& instance();
};

}  // namespace core
