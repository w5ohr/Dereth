// Workspace: custom models, knowledge bases, prompts, tools, functions, skills.
#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

enum class Tab { Models, Knowledge, Prompts, Tools, Functions, Skills };

Tab gTab = Tab::Models;
bool gLoaded[6] = {false, false, false, false, false, false};
std::string gFilter;
std::string gSelected;

// Prompt editor state.
std::string gPromptCommand;
std::string gPromptTitle;
std::string gPromptContent;
bool gPromptEditing = false;

// Knowledge editor state.
std::string gKnowledgeName;
std::string gKnowledgeDescription;
bool gKnowledgeCreating = false;

const char* tabName(Tab tab) {
  switch (tab) {
    case Tab::Models: return "Models";
    case Tab::Knowledge: return "Knowledge";
    case Tab::Prompts: return "Prompts";
    case Tab::Tools: return "Tools";
    case Tab::Functions: return "Functions";
    case Tab::Skills: return "Skills";
  }
  return "";
}

}  // namespace

void App::drawWorkspacePane() {
  auto invalidate = [](Tab tab) { gLoaded[static_cast<int>(tab)] = false; };

  auto ensureLoaded = [&](Tab tab) {
    int index = static_cast<int>(tab);
    if (gLoaded[index]) return;
    gLoaded[index] = true;
    switch (tab) {
      case Tab::Models:
        async::run([this] {
          api::Result res = api_.customModels();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) customModelCache = res.data;
          });
        });
        break;
      case Tab::Knowledge:
        async::run([this] {
          api::Result res = api_.knowledgeList();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) knowledgeList = res.data;
          });
        });
        break;
      case Tab::Prompts:
        async::run([this] {
          api::Result res = api_.prompts();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) promptList = res.data;
          });
        });
        break;
      case Tab::Tools:
        async::run([this] {
          api::Result res = api_.tools();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) toolList = res.data;
          });
        });
        break;
      case Tab::Functions:
        async::run([this] {
          api::Result res = api_.functions();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) functionList = res.data;
          });
        });
        break;
      case Tab::Skills:
        async::run([this] {
          api::Result res = api_.skills();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) skillList = res.data;
          });
        });
        break;
    }
  };

  paneHeader("Workspace", "Models, knowledge, prompts and extensions", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    if (ImGui::Button("Refresh", ImVec2(90, 30))) {
      for (bool& loaded : gLoaded) loaded = false;
    }
  });

  paneBody("##workspace");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;

  if (ImGui::BeginTabBar("##wstabs")) {
    for (Tab tab : {Tab::Models, Tab::Knowledge, Tab::Prompts, Tab::Tools, Tab::Functions,
                    Tab::Skills}) {
      if (ImGui::BeginTabItem(tabName(tab))) {
        gTab = tab;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  ensureLoaded(gTab);
  ImGui::Dummy(ImVec2(0, 8));
  widgets::searchField("wsfilter", &gFilter, "Filter", 320.0f);
  ImGui::Dummy(ImVec2(0, 10));

  auto matches = [&](const std::string& text) {
    return gFilter.empty() || core::containsNoCase(text, gFilter);
  };

  switch (gTab) {
    case Tab::Models: {
      if (customModelCache.empty()) emptyState("No custom models defined on this server");
      for (const auto& model : customModelCache) {
        const std::string id = model.value("id", "");
        const std::string name = model.value("name", id);
        if (!matches(name) && !matches(id)) continue;
        const Json& meta = model.value("meta", Json::object());
        ImGui::PushID(id.c_str());
        card(id.c_str(), [&] {
          theme::pushBold();
          ImGui::TextUnformatted(name.c_str());
          theme::pop();
          ImGui::SameLine();
          widgets::dimText("%s", id.c_str());
          const std::string description = meta.value("description", "");
          if (!description.empty()) {
            ImGui::PushTextWrapPos(width - 40.0f);
            widgets::dimText("%s", description.c_str());
            ImGui::PopTextWrapPos();
          }
          const std::string base = model.value("base_model_id", "");
          if (!base.empty()) field("Base model", base);
          if (ImGui::SmallButton("Use in new chat")) {
            selectedModels = {id};
            settings().selectedModels = selectedModels;
            newChat();
          }
          ImGui::SameLine();
          if (ImGui::SmallButton(model.value("is_active", true) ? "Disable" : "Enable")) {
            async::run([this, id, invalidate] {
              api_.toggleModel(id);
              async::post([invalidate] { invalidate(Tab::Models); });
            });
          }
        }, width);
        ImGui::PopID();
      }
      break;
    }

    case Tab::Knowledge: {
      card("newkb", [&] {
        if (!gKnowledgeCreating) {
          if (widgets::primaryButton("New knowledge base", ImVec2(200, 0))) {
            gKnowledgeCreating = true;
          }
        } else {
          ImGui::SetNextItemWidth(260);
          ImGui::InputTextWithHint("##kbname", "Name", &gKnowledgeName);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(320);
          ImGui::InputTextWithHint("##kbdesc", "Description", &gKnowledgeDescription);
          ImGui::SameLine();
          if (widgets::primaryButton("Create", ImVec2(100, 0)) &&
              !core::trim(gKnowledgeName).empty()) {
            const std::string name = gKnowledgeName;
            const std::string description = gKnowledgeDescription;
            gKnowledgeName.clear();
            gKnowledgeDescription.clear();
            gKnowledgeCreating = false;
            async::run([this, name, description, invalidate] {
              api::Result res = api_.createKnowledge(name, description);
              async::post([res, invalidate] {
                if (res.ok) {
                  invalidate(Tab::Knowledge);
                } else {
                  widgets::toastError(api::errorMessage(res));
                }
              });
            });
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(90, 0))) gKnowledgeCreating = false;
        }
      }, width);

      if (knowledgeList.empty()) emptyState("No knowledge bases yet");
      for (const auto& knowledge : knowledgeList) {
        const std::string id = knowledge.value("id", "");
        const std::string name = knowledge.value("name", "Untitled");
        if (!matches(name)) continue;
        ImGui::PushID(id.c_str());
        card(id.c_str(), [&] {
          theme::pushBold();
          ImGui::TextUnformatted(name.c_str());
          theme::pop();
          widgets::dimText("%s", knowledge.value("description", "").c_str());
          const Json& files = knowledge.value("files", Json::array());
          widgets::dimText("%d file(s)", static_cast<int>(files.size()));
          for (const auto& file : files) {
            ImGui::BulletText("%s", file.value("meta", Json::object())
                                        .value("name", file.value("filename", "file"))
                                        .c_str());
          }
          if (ImGui::SmallButton("Add files")) {
            for (const std::string& path : platform::openFiles(true)) {
              bool ok = false;
              const std::string bytes = core::readFile(path, &ok);
              if (!ok) continue;
              const std::string fileName = core::baseName(path);
              const std::string mime = core::mimeForExtension(core::extension(path));
              async::run([this, id, fileName, mime, bytes, invalidate] {
                api::Result upload = api_.uploadFile(fileName, mime, bytes);
                if (upload.ok && upload.data.contains("id")) {
                  api_.addFileToKnowledge(id, upload.data["id"]);
                }
                async::post([invalidate, upload] {
                  if (!upload.ok) widgets::toastError(api::errorMessage(upload));
                  invalidate(Tab::Knowledge);
                });
              });
            }
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Delete")) {
            async::run([this, id, invalidate] {
              api_.deleteKnowledge(id);
              async::post([invalidate] { invalidate(Tab::Knowledge); });
            });
          }
        }, width);
        ImGui::PopID();
      }
      break;
    }

    case Tab::Prompts: {
      card("newprompt", [&] {
        if (!gPromptEditing) {
          if (widgets::primaryButton("New prompt", ImVec2(160, 0))) gPromptEditing = true;
        } else {
          ImGui::SetNextItemWidth(200);
          ImGui::InputTextWithHint("##cmd", "/command", &gPromptCommand);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(260);
          ImGui::InputTextWithHint("##ptitle", "Title", &gPromptTitle);
          ImGui::InputTextMultiline("##pcontent", &gPromptContent,
                                    ImVec2(width - 60.0f, 90.0f));
          if (widgets::primaryButton("Save", ImVec2(100, 0)) &&
              !core::trim(gPromptCommand).empty()) {
            std::string command = core::trim(gPromptCommand);
            if (command[0] == '/') command.erase(0, 1);
            Json body{{"command", "/" + command},
                      {"name", gPromptTitle.empty() ? command : gPromptTitle},
                      {"title", gPromptTitle.empty() ? command : gPromptTitle},
                      {"content", gPromptContent}};
            gPromptCommand.clear();
            gPromptTitle.clear();
            gPromptContent.clear();
            gPromptEditing = false;
            async::run([this, body, invalidate] {
              api::Result res = api_.createPrompt(body);
              async::post([res, invalidate] {
                if (res.ok) {
                  invalidate(Tab::Prompts);
                } else {
                  widgets::toastError(api::errorMessage(res));
                }
              });
            });
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(90, 0))) gPromptEditing = false;
        }
      }, width);

      if (promptList.empty()) emptyState("No saved prompts");
      for (const auto& prompt : promptList) {
        const std::string command = prompt.value("command", "");
        const std::string title = prompt.value("title", prompt.value("name", command));
        if (!matches(title) && !matches(command)) continue;
        ImGui::PushID(command.c_str());
        card(command.c_str(), [&] {
          theme::pushBold();
          ImGui::TextUnformatted(title.c_str());
          theme::pop();
          ImGui::SameLine();
          widgets::dimText("%s", command.c_str());
          ImGui::PushTextWrapPos(width - 40.0f);
          widgets::dimText("%s", prompt.value("content", "").substr(0, 400).c_str());
          ImGui::PopTextWrapPos();
          if (ImGui::SmallButton("Use")) {
            composer = prompt.value("content", "");
            pane = Pane::Chat;
            focusComposer = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Copy")) {
            platform::setClipboardText(prompt.value("content", ""));
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Delete")) {
            const std::string id = prompt.value("id", command);
            async::run([this, id, invalidate] {
              api_.deletePrompt(id);
              async::post([invalidate] { invalidate(Tab::Prompts); });
            });
          }
        }, width);
        ImGui::PopID();
      }
      break;
    }

    case Tab::Tools:
    case Tab::Functions:
    case Tab::Skills: {
      const Json& list = gTab == Tab::Tools ? toolList
                                            : (gTab == Tab::Functions ? functionList : skillList);
      if (list.empty()) {
        emptyState(gTab == Tab::Tools ? "No tools installed"
                                      : (gTab == Tab::Functions ? "No functions installed"
                                                                : "No skills installed"));
      }
      for (const auto& item : list) {
        const std::string id = item.value("id", "");
        const std::string name = item.value("name", id);
        if (!matches(name) && !matches(id)) continue;
        const Json& meta = item.value("meta", Json::object());
        ImGui::PushID(id.c_str());
        card(id.c_str(), [&] {
          theme::pushBold();
          ImGui::TextUnformatted(name.c_str());
          theme::pop();
          ImGui::SameLine();
          widgets::dimText("%s", id.c_str());
          ImGui::PushTextWrapPos(width - 40.0f);
          widgets::dimText("%s", meta.value("description", "").c_str());
          ImGui::PopTextWrapPos();
          const bool active = item.value("is_active", true);
          if (gTab == Tab::Tools) {
            bool enabled = std::find(activeToolIds.begin(), activeToolIds.end(), id) !=
                           activeToolIds.end();
            if (ImGui::Checkbox("Use in chat", &enabled)) {
              if (enabled) {
                activeToolIds.push_back(id);
              } else {
                activeToolIds.erase(std::remove(activeToolIds.begin(), activeToolIds.end(), id),
                                    activeToolIds.end());
              }
            }
          } else if (ImGui::SmallButton(active ? "Disable" : "Enable")) {
            const Tab tab = gTab;
            async::run([this, id, tab, invalidate] {
              if (tab == Tab::Functions) {
                api_.toggleFunction(id);
              } else {
                api_.toggleSkill(id);
              }
              async::post([invalidate, tab] { invalidate(tab); });
            });
          }
        }, width);
        ImGui::PopID();
      }
      break;
    }
  }

  paneEnd();
}

}  // namespace app
