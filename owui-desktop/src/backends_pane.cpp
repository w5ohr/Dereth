// Direct backends: the registry (Settings > Direct backends), the keys in the
// keychain, the model lists, and the backend + model button in a private
// chat's header. The Ollama URL from Settings is always the first backend;
// the stored ones live in settings extra["backends"] (no keys there).
#include "app.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <map>

#include "core/async.h"
#include "core/backends.h"
#include "core/common.h"
#include "core/keychain.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr const char* kImplicitOllama = "ollama";

bool useKeychain() { return std::getenv("OWUI_NO_KEYCHAIN") == nullptr; }
std::string account(const std::string& id) { return "backend:" + id; }
std::string keysFile() { return core::supportDir() + "/backend-keys.json"; }

// OWUI_NO_KEYCHAIN=1 (scratch homes) keeps keys in a 0600 file instead.
std::string loadKey(const std::string& id) {
  if (useKeychain()) return keychain::load(account(id));
  bool ok = false;
  Json j = Json::parse(core::readFile(keysFile(), &ok), nullptr, false);
  if (!ok || j.is_discarded() || !j.is_object()) return "";
  return j.value(id, "");
}

void saveKey(const std::string& id, const std::string& key) {
  if (useKeychain()) {
    if (key.empty()) keychain::erase(account(id));
    else keychain::save(account(id), key);
    return;
  }
  bool ok = false;
  Json j = Json::parse(core::readFile(keysFile(), &ok), nullptr, false);
  if (!ok || j.is_discarded() || !j.is_object()) j = Json::object();
  if (key.empty()) j.erase(id);
  else j[id] = key;
  core::writeFile(keysFile(), j.dump());
  chmod(keysFile().c_str(), 0600);
}

struct ModelCache {
  std::vector<std::string> models;
  std::string error;
  bool loading = false;
  bool loaded = false;
};
std::map<std::string, ModelCache> gModels;

// Add form.
std::string gNewName, gNewUrl, gNewKey;
int gNewKind = 1;   // OpenAI-compatible is the common case
std::map<std::string, std::string> gKeyEdits;
std::string gNote;   // last test result

const char* kKinds[] = {"Ollama", "OpenAI-compatible", "Anthropic"};
backends::Kind kindAt(int index) {
  return index == 0 ? backends::Kind::Ollama : index == 2 ? backends::Kind::Anthropic : backends::Kind::OpenAI;
}

}  // namespace

std::vector<backends::Backend> App::backendList() {
  std::vector<backends::Backend> out;
  if (!core::trim(settings().ollamaUrl).empty()) {
    backends::Backend b;
    b.id = kImplicitOllama;
    b.name = "Ollama";
    b.kind = backends::Kind::Ollama;
    b.url = backends::normalizeUrl(settings().ollamaUrl, b.kind);
    out.push_back(b);
  }
  for (const backends::Backend& b : backends::fromJson(settings().extra.value("backends", Json::array()))) {
    out.push_back(b);
  }
  return out;
}

bool App::findBackend(const std::string& id, backends::Backend* out) {
  for (const backends::Backend& b : backendList()) {
    if (b.id == id) {
      *out = b;
      return true;
    }
  }
  return false;
}

std::string App::backendKey(const std::string& id) { return id == kImplicitOllama ? "" : loadKey(id); }

agents::Transport App::backendTransport(const backends::Backend& backend) {
  const std::string key = backendKey(backend.id);
  return [backend, key](const Json& payload, const std::function<bool(const Json&)>& onLine,
                        const std::atomic<bool>* cancel) {
    const backends::StreamResult r = backends::streamChat(backend, key, payload, onLine, cancel);
    api::Result out;
    out.ok = r.ok;
    out.status = r.status;
    out.error = r.error;
    return out;
  };
}

void App::refreshBackendModels(const std::string& id, bool force) {
  backends::Backend backend;
  if (!findBackend(id, &backend)) return;
  ModelCache& cache = gModels[id];
  if (cache.loading || (cache.loaded && !force)) return;
  cache.loading = true;
  const std::string key = backendKey(id);
  async::run([backend, key, id] {
    std::string error;
    std::vector<std::string> models = backends::listModels(backend, key, &error);
    async::post([id, models, error] {
      ModelCache& c = gModels[id];
      c.models = models;
      c.error = error;
      c.loading = false;
      c.loaded = true;
    });
  });
}

void App::drawBackendsSection(float width) {
  (void)width;
  const auto& palette = theme::palette();
  widgets::dimText("Talk to a model server with no Open WebUI in between. Keys go to the macOS keychain, never to disk or the server.");
  widgets::dimText("The Ollama URL below is always the first backend. Private chats pick a backend from the button in their header.");
  ImGui::Dummy(ImVec2(0, 4));

  std::vector<backends::Backend> stored = backends::fromJson(settings().extra.value("backends", Json::array()));
  int removeIndex = -1;
  for (size_t i = 0; i < stored.size(); ++i) {
    const backends::Backend& b = stored[i];
    ImGui::PushID(b.id.c_str());
    theme::pushBold();
    ImGui::TextUnformatted(b.name.c_str());
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("%s  %s", backends::kindLabel(b.kind).c_str(), b.url.c_str());
    ImGui::SameLine();
    const bool hasKey = !loadKey(b.id).empty();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(hasKey ? palette.success : palette.textDim));
    ImGui::TextUnformatted(hasKey ? "key set" : "no key");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("Test")) {
      gNote = "Testing " + b.name + "...";
      const std::string key = loadKey(b.id);
      async::run([b, key] {
        std::string error;
        const std::vector<std::string> models = backends::listModels(b, key, &error);
        async::post([b, models, error] {
          gNote = error.empty() ? b.name + ": " + std::to_string(models.size()) + " model" + (models.size() == 1 ? "" : "s") +
                                      (models.empty() ? "" : " (" + models.front() + (models.size() > 1 ? ", ..." : "") + ")")
                                : b.name + ": " + error;
          ModelCache& c = gModels[b.id];
          if (error.empty()) {
            c.models = models;
            c.error.clear();
            c.loaded = true;
          }
        });
      });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    std::string& edit = gKeyEdits[b.id];
    ImGui::InputTextWithHint("##keyedit", hasKey ? "replace API key" : "API key", &edit, ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::SmallButton(hasKey && edit.empty() ? "Forget key" : "Save key")) {
      saveKey(b.id, core::trim(edit));
      edit.clear();
      gModels.erase(b.id);
      widgets::toast(widgets::ToastLevel::Success, useKeychain() ? "Key saved to the keychain" : "Key saved");
    }
    ImGui::PopID();
  }
  if (removeIndex >= 0) {
    saveKey(stored[removeIndex].id, "");
    gModels.erase(stored[removeIndex].id);
    stored.erase(stored.begin() + removeIndex);
    settings().extra["backends"] = backends::toJson(stored);
    settings().save();
  }
  if (!gNote.empty()) widgets::dimText("%s", gNote.c_str());

  ImGui::Separator();
  ImGui::SetNextItemWidth(150);
  ImGui::InputTextWithHint("##bname", "Name", &gNewName);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  ImGui::Combo("##bkind", &gNewKind, kKinds, 3);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(260);
  const char* hint = gNewKind == 0 ? "http://host:11434" : gNewKind == 2 ? "https://api.anthropic.com" : "http://localhost:1234/v1";
  ImGui::InputTextWithHint("##burl", hint, &gNewUrl);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180);
  ImGui::InputTextWithHint("##bkey", "API key (optional)", &gNewKey, ImGuiInputTextFlags_Password);
  ImGui::SameLine();
  if (ImGui::Button("Add") && !core::trim(gNewUrl).empty()) {
    backends::Backend b;
    b.id = backends::newId();
    b.kind = kindAt(gNewKind);
    b.url = backends::normalizeUrl(gNewUrl, b.kind);
    b.name = core::trim(gNewName).empty() ? b.url : core::trim(gNewName);
    stored.push_back(b);
    settings().extra["backends"] = backends::toJson(stored);
    settings().save();
    if (!core::trim(gNewKey).empty()) saveKey(b.id, core::trim(gNewKey));
    gNewName.clear();
    gNewUrl.clear();
    gNewKey.clear();
    widgets::toast(widgets::ToastLevel::Success, "Added " + b.name + " - Test lists its models");
  }
  widgets::dimText("OpenAI-compatible covers OpenAI, LM Studio (http://localhost:1234), llama.cpp server (http://localhost:8080), vLLM, OpenRouter, Groq and the like; the /v1 is added when missing.");
}

void App::drawPrivateBackendButton() {
  const auto& palette = theme::palette();
  const std::vector<backends::Backend> list = backendList();
  std::string backendId = chat.backendId.empty() ? (list.empty() ? "" : list.front().id) : chat.backendId;
  backends::Backend current;
  const bool have = findBackend(backendId, &current);
  const std::string model = chat.models.empty() ? "" : chat.models.front();
  const std::string label = !have ? "No backend" : current.name + (model.empty() ? ": pick a model" : ": " + model);
  if (ImGui::Button((label + "  v##privbackend").c_str())) {
    ImGui::OpenPopup("##privbackendpopup");
    if (have) refreshBackendModels(backendId, false);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Private chat: replies come straight from this backend, never through the server");
  }
  if (!ImGui::BeginPopup("##privbackendpopup")) return;
  widgets::sectionHeader("BACKEND");
  for (const backends::Backend& b : list) {
    const std::string row = b.name + "  (" + backends::kindLabel(b.kind) + ")";
    if (ImGui::Selectable(row.c_str(), b.id == backendId, ImGuiSelectableFlags_NoAutoClosePopups) && b.id != backendId) {
      chat.backendId = b.id;
      backendId = b.id;
      chat.models.clear();
      refreshBackendModels(b.id, false);
      savePrivateChat();
    }
  }
  if (list.empty()) widgets::dimText("No backends yet.");
  if (ImGui::SmallButton("Manage backends...")) {
    pane = Pane::Settings;
    ImGui::CloseCurrentPopup();
  }
  ImGui::Separator();
  widgets::sectionHeader("MODEL");
  if (have) {
    ModelCache& cache = gModels[backendId];
    if (cache.loading) {
      widgets::dimText("Loading models...");
    } else if (!cache.error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::TextWrapped("%s", cache.error.c_str());
      ImGui::PopStyleColor();
    } else if (cache.models.empty()) {
      widgets::dimText("This backend lists no models.");
    } else {
      static std::string filter;
      ImGui::SetNextItemWidth(340);
      ImGui::InputTextWithHint("##bmfilter", "Filter", &filter);
      ImGui::BeginChild("##bmodels", ImVec2(340, std::min(260.0f, 8.0f + 22.0f * static_cast<float>(cache.models.size()))));
      for (const std::string& m : cache.models) {
        if (!filter.empty() && !core::containsNoCase(m, filter)) continue;
        if (ImGui::Selectable(m.c_str(), m == model)) {
          chat.models = {m};
          savePrivateChat();
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndChild();
    }
    if (ImGui::SmallButton("Refresh")) refreshBackendModels(backendId, true);
  }
  ImGui::EndPopup();
}

}  // namespace app
