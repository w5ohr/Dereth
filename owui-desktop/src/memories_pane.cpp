// Memories: the long-term facts the server injects into conversations.
#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

bool gLoaded = false;
bool gLoading = false;
std::string gNewMemory;
std::string gQuery;
Json gQueryResult = Json::object();
std::string gEditingId;
std::string gEditBuffer;

}  // namespace

void App::drawMemoriesPane() {
  auto reload = [] {
    gLoaded = false;
    gLoading = false;
  };

  if (!gLoaded && !gLoading) {
    gLoading = true;
    async::run([this] {
      api::Result res = api_.memories();
      async::post([this, res] {
        gLoading = false;
        gLoaded = true;
        if (res.ok && res.data.is_array()) memoryList = res.data;
      });
    });
  }

  paneHeader("Memories", "Facts the assistant remembers about you", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    if (ImGui::Button("Refresh", ImVec2(90, 30))) reload();
  });

  paneBody("##memories");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;

  card("add", [&] {
    ImGui::TextUnformatted("Add a memory");
    ImGui::SetNextItemWidth(width - 140.0f);
    const bool submitted = ImGui::InputTextWithHint(
        "##newmemory", "e.g. I prefer concise answers in British English", &gNewMemory,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((widgets::primaryButton("Add", ImVec2(100, 0)) || submitted) &&
        !core::trim(gNewMemory).empty()) {
      const std::string content = core::trim(gNewMemory);
      gNewMemory.clear();
      async::run([this, content, reload] {
        api::Result res = api_.addMemory(content);
        async::post([res, reload] {
          if (res.ok) {
            widgets::toast(widgets::ToastLevel::Success, "Memory added");
            reload();
          } else {
            widgets::toastError(api::errorMessage(res));
          }
        });
      });
    }
  }, width);

  card("search", [&] {
    ImGui::TextUnformatted("Search memories");
    ImGui::SetNextItemWidth(width - 140.0f);
    const bool submitted = ImGui::InputTextWithHint("##memquery", "Semantic search", &gQuery,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Search", ImVec2(100, 0)) || submitted) && !gQuery.empty()) {
      const std::string query = gQuery;
      async::run([this, query] {
        api::Result res = api_.queryMemory(query, 5);
        async::post([res] { gQueryResult = res.ok ? res.data : Json::object(); });
      });
    }
    if (!gQueryResult.empty()) {
      ImGui::PushTextWrapPos(width - 20.0f);
      widgets::dimText("%s", gQueryResult.dump(2).substr(0, 1200).c_str());
      ImGui::PopTextWrapPos();
    }
  }, width);

  if (gLoading) widgets::loadingRow("Loading memories...");
  if (memoryList.empty() && !gLoading) emptyState("No memories stored yet");

  for (const auto& memory : memoryList) {
    const std::string id = memory.value("id", "");
    const std::string content = memory.value("content", "");
    ImGui::PushID(id.c_str());
    card(id.c_str(), [&] {
      if (gEditingId == id) {
        ImGui::SetNextItemWidth(width - 220.0f);
        ImGui::InputText("##edit", &gEditBuffer);
        ImGui::SameLine();
        if (widgets::primaryButton("Save", ImVec2(80, 0))) {
          const std::string text = gEditBuffer;
          gEditingId.clear();
          async::run([this, id, text, reload] {
            api_.updateMemory(id, text);
            async::post([reload] { reload(); });
          });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) gEditingId.clear();
      } else {
        ImGui::PushTextWrapPos(width - 200.0f);
        ImGui::TextUnformatted(content.c_str());
        ImGui::PopTextWrapPos();
        widgets::dimText("%s",
                         core::relativeTime(memory.value("updated_at",
                                                         memory.value("created_at",
                                                                      static_cast<int64_t>(0))))
                             .c_str());
        ImGui::SameLine(width - 180.0f);
        if (ImGui::SmallButton("Edit")) {
          gEditingId = id;
          gEditBuffer = content;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
          async::run([this, id, reload] {
            api_.deleteMemory(id);
            async::post([reload] { reload(); });
          });
        }
      }
    }, width);
    ImGui::PopID();
  }

  paneEnd();
}

}  // namespace app
