#include "core/settings.h"

#include "core/common.h"

namespace core {
namespace {

std::string path() { return supportDir() + "/settings.json"; }

template <typename T>
void pick(const nlohmann::json& j, const char* key, T& out) {
  if (j.contains(key)) {
    try {
      out = j.at(key).get<T>();
    } catch (...) {
    }
  }
}

}  // namespace

Settings& Settings::instance() {
  static Settings s;
  return s;
}

void Settings::load() {
  bool ok = false;
  std::string raw = readFile(path(), &ok);
  if (!ok || raw.empty()) return;
  nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;

  pick(j, "serverUrl", serverUrl);
  pick(j, "email", email);
  pick(j, "rememberSession", rememberSession);
  pick(j, "hasSavedSession", hasSavedSession);
  pick(j, "ollamaUrl", ollamaUrl);
  pick(j, "comfyUrl", comfyUrl);
  pick(j, "theme", theme);
  pick(j, "fontSize", fontSize);
  pick(j, "uiScale", uiScale);
  pick(j, "sidebarWidth", sidebarWidth);
  pick(j, "showTimestamps", showTimestamps);
  pick(j, "renderMarkdown", renderMarkdown);
  pick(j, "autoTitle", autoTitle);
  pick(j, "autoFollowUps", autoFollowUps);
  pick(j, "streamResponses", streamResponses);
  pick(j, "sendOnEnter", sendOnEnter);
  pick(j, "notifyOnCompletion", notifyOnCompletion);
  pick(j, "speakReplies", speakReplies);
  pick(j, "ttsVoice", ttsVoice);
  pick(j, "selectedModels", selectedModels);
  pick(j, "recentServers", recentServers);
  if (j.contains("extra") && j["extra"].is_object()) extra = j["extra"];
}

void Settings::save() const {
  nlohmann::json j{
      {"serverUrl", serverUrl},
      {"email", email},
      {"rememberSession", rememberSession},
      {"hasSavedSession", hasSavedSession},
      {"ollamaUrl", ollamaUrl},
      {"comfyUrl", comfyUrl},
      {"theme", theme},
      {"fontSize", fontSize},
      {"uiScale", uiScale},
      {"sidebarWidth", sidebarWidth},
      {"showTimestamps", showTimestamps},
      {"renderMarkdown", renderMarkdown},
      {"autoTitle", autoTitle},
      {"autoFollowUps", autoFollowUps},
      {"streamResponses", streamResponses},
      {"sendOnEnter", sendOnEnter},
      {"notifyOnCompletion", notifyOnCompletion},
      {"speakReplies", speakReplies},
      {"ttsVoice", ttsVoice},
      {"selectedModels", selectedModels},
      {"recentServers", recentServers},
      {"extra", extra},
  };
  writeFile(path(), j.dump(2));
}

}  // namespace core
