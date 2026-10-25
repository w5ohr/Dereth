// Plugins in the app: the host's callbacks land on the UI thread, the plugins
// folder is watched for hot reload, commands show in the palette, tools join
// the MCP tool list, before-send hooks run in sendMessage, renderers add a
// button under code blocks, and Tools > Plugins lists what is loaded.
#include "app.h"

#include <dirent.h>
#include <sys/stat.h>

#include "core/async.h"
#include "core/common.h"
#include "native.h"
#include "platform.h"
#include "plugins/plugin_host.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

// Names and mtimes of the *.js files: the folder watcher also fires for the
// plugins' own storage writes under .data/, so reloads key off this instead.
std::string pluginSignature(const std::string& dir) {
  std::string out;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* entry = readdir(d)) {
      const std::string name = entry->d_name;
      if (name.size() < 4 || name.substr(name.size() - 3) != ".js") continue;
      struct stat st {};
      if (stat((dir + "/" + name).c_str(), &st) == 0) {
        out += name + ":" + std::to_string(st.st_mtimespec.tv_sec) + "." + std::to_string(st.st_mtimespec.tv_nsec) +
               ":" + std::to_string(st.st_size) + ";";
      }
    }
    closedir(d);
  }
  return out;
}

std::string gLoadedSignature;

}  // namespace

void App::pluginsInit() {
  plugins::Host& host = plugins::Host::instance();
  host.setDirectory(core::supportDir() + "/plugins");
  plugins::HostCallbacks cb;
  cb.toast = [](const std::string& text) { async::post([text] { widgets::toastInfo(text); }); };
  cb.notify = [](const std::string& title, const std::string& body) {
    async::post([title, body] { native::notifyWithReply(title, body, ""); });
  };
  cb.insert = [this](const std::string& text) {
    async::post([this, text] {
      composer += text;
      ++composerGeneration;
      focusComposer = true;
      pane = Pane::Chat;
    });
  };
  cb.send = [this](const std::string& text) {
    async::post([this, text] {
      pane = Pane::Chat;
      sendMessage(text);
      ++composerGeneration;
    });
  };
  cb.openArtifact = [this](const std::string& html) {
    async::post([this, html] {
      pane = Pane::Chat;
      openArtifact(html, "html");
    });
  };
  cb.clipboardRead = [] { return platform::clipboardText(); };
  cb.clipboardWrite = [](const std::string& text) { async::post([text] { platform::setClipboardText(text); }); };
  cb.log = [](const std::string& pluginId, const std::string& line) {
    core::debugLog("plugin %s: %s", pluginId.c_str(), line.c_str());
  };
  cb.allowNetwork = settings().extra.value("pluginNetwork", true);
  cb.allowFiles = settings().extra.value("pluginFiles", true);
  host.setCallbacks(cb);
  if (!settings().extra.value("pluginsEnabled", true)) return;
  host.loadAll();
  gLoadedSignature = pluginSignature(host.directory());

  // Hot reload: FSEvents fires on its own thread; coalesce bursts of saves.
  mkdir(host.directory().c_str(), 0755);
  pluginWatchToken = native::watchFolder(host.directory(), [this](const std::vector<std::string>&) {
    async::post([this] { pluginReloadAt = core::nowMillis() + 400.0; });
  });
}

void App::pluginsShutdown() {
  if (pluginWatchToken >= 0) native::stopWatching(pluginWatchToken);
  pluginWatchToken = -1;
  plugins::Host::instance().unloadAll();
}

void App::pluginsReload() {
  plugins::Host& host = plugins::Host::instance();
  host.loadAll();
  gLoadedSignature = pluginSignature(host.directory());
  int errors = 0;
  for (const plugins::PluginInfo& info : host.plugins()) {
    if (!info.ok) ++errors;
  }
  const size_t count = host.plugins().size();
  widgets::toastInfo(std::to_string(count) + (count == 1 ? " plugin" : " plugins") + " loaded" +
                     (errors ? " - " + std::to_string(errors) + " with errors (see Tools > Plugins)" : ""));
}

// Called every frame: a pending hot reload fires once the folder is quiet.
void App::pluginsTick() {
  if (pluginReloadAt > 0.0 && core::nowMillis() >= pluginReloadAt) {
    pluginReloadAt = 0.0;
    if (!settings().extra.value("pluginsEnabled", true)) return;
    // Only a changed, added or removed .js file reloads; storage writes don't.
    if (pluginSignature(plugins::Host::instance().directory()) != gLoadedSignature) pluginsReload();
  }
}

// What a plugin sees as `ctx`: the visible branch of the open chat (trimmed)
// and the current model.
Json App::pluginSnapshot() const {
  Json messages = Json::array();
  const std::vector<std::string> path = chat.branch();
  const size_t start = path.size() > 40 ? path.size() - 40 : 0;
  for (size_t i = start; i < path.size(); ++i) {
    auto it = chat.messages.find(path[i]);
    if (it == chat.messages.end()) continue;
    messages.push_back(Json{{"id", it->second.id},
                            {"role", it->second.role},
                            {"content", it->second.content.size() > 20000 ? it->second.content.substr(0, 20000)
                                                                           : it->second.content},
                            {"model", it->second.model}});
  }
  return Json{{"chat", Json{{"id", chat.id}, {"title", chat.title}, {"messages", messages}}},
              {"model", selectedModels.empty() ? "" : selectedModels.front()},
              {"composer", composer},
              {"user", user_.value("name", "")}};
}

void App::drawPluginsSection(float width) {
  const auto& palette = theme::palette();
  plugins::Host& host = plugins::Host::instance();
  widgets::sectionHeader("PLUGINS");
  widgets::dimText("JavaScript files in the plugins folder add palette commands, tools the model can call,\n"
                   "prompt rewrites and code-block renderers. They reload when saved.");

  bool enabled = settings().extra.value("pluginsEnabled", true);
  if (ImGui::Checkbox("Load plugins", &enabled)) {
    settings().extra["pluginsEnabled"] = enabled;
    settings().save();
    if (enabled) pluginsReload();
    else host.unloadAll();
  }
  ImGui::SameLine(0, 16);
  bool network = settings().extra.value("pluginNetwork", true);
  if (ImGui::Checkbox("Allow owui.fetch", &network)) {
    settings().extra["pluginNetwork"] = network;
    settings().save();
    pluginsInit();   // re-sets the callbacks' permissions
  }
  ImGui::SameLine(0, 16);
  bool files = settings().extra.value("pluginFiles", true);
  if (ImGui::Checkbox("Allow file access", &files)) {
    settings().extra["pluginFiles"] = files;
    settings().save();
    pluginsInit();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
  if (ImGui::Button("Reload")) pluginsReload();
  ImGui::SameLine();
  if (ImGui::Button("Open folder")) {
    mkdir(host.directory().c_str(), 0755);
    platform::revealInFinder(host.directory());
  }
  ImGui::SameLine();
  if (ImGui::Button("Write example plugin")) {
    const std::string path = host.writeExample();
    if (path.empty()) {
      widgets::toastInfo("The folder already has plugins - open it to add more");
    } else {
      pluginsReload();
      platform::revealInFinder(path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("API reference")) platform::openUrl("https://github.com/w5ohr/Dereth/blob/main/owui-desktop/docs/PLUGINS.md");
  ImGui::PopStyleVar();
  widgets::dimText("%s", host.directory().c_str());
  ImGui::Dummy(ImVec2(0, 6));

  const std::vector<plugins::PluginInfo> list = host.plugins();
  if (list.empty()) {
    widgets::dimText(enabled ? "No plugins yet. \"Write example plugin\" puts a documented one in the folder."
                             : "Plugins are switched off.");
    return;
  }
  for (const plugins::PluginInfo& info : list) {
    ImGui::PushID(info.id.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
    ImGui::BeginChild("##plugin", ImVec2(width - 20.0f, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    icons::inlineIcon(info.ok ? icons::Icon::Check : icons::Icon::Warning, info.ok ? palette.success : palette.danger);
    ImGui::SameLine(0, 6);
    theme::pushBold();
    ImGui::TextUnformatted(info.name.c_str());
    theme::pop();
    if (!info.version.empty()) {
      ImGui::SameLine();
      widgets::dimText("v%s", info.version.c_str());
    }
    ImGui::SameLine();
    widgets::dimText("%s", info.id.c_str());
    if (!info.description.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 60.0f);
      ImGui::TextUnformatted(info.description.c_str());
      ImGui::PopTextWrapPos();
    }
    widgets::dimText("%d command%s - %d tool%s - %d before-send hook%s - %d renderer%s", info.commands,
                     info.commands == 1 ? "" : "s", info.tools, info.tools == 1 ? "" : "s", info.hooks,
                     info.hooks == 1 ? "" : "s", info.renderers, info.renderers == 1 ? "" : "s");
    for (const std::string& error : info.errors) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 60.0f);
      ImGui::TextUnformatted(error.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
    }
    if (!info.log.empty()) {
      if (ImGui::TreeNode("Log")) {
        theme::pushMono();
        const size_t start = info.log.size() > 12 ? info.log.size() - 12 : 0;
        for (size_t i = start; i < info.log.size(); ++i) widgets::dimText("%s", info.log[i].c_str());
        theme::pop();
        ImGui::TreePop();
      }
    }
    if (widgets::ghostButton("Reveal")) platform::revealInFinder(info.path);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4));
  }
}

}  // namespace app
