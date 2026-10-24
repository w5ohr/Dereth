// Notes: server-side markdown notes with an editor and live preview.
#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "ui/icons.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

bool gLoaded = false;
bool gLoading = false;
std::string gSelectedId;
std::string gTitle;
std::string gContent;
bool gDirty = false;
bool gPreview = false;
std::string gFilter;

std::string noteContent(const Json& note) {
  const Json& data = note.value("data", Json::object());
  const Json& content = data.value("content", Json());
  if (content.is_string()) return content.get<std::string>();
  if (content.is_object()) {
    if (content.contains("md") && content["md"].is_string()) return content["md"];
    if (content.contains("html") && content["html"].is_string()) return content["html"];
  }
  return "";
}

}  // namespace

void App::drawNotesPane() {
  if (!gLoaded && !gLoading) {
    gLoading = true;
    async::run([this] {
      api::Result res = api_.notes();
      async::post([this, res] {
        gLoading = false;
        gLoaded = true;
        if (res.ok && res.data.is_array()) noteList = res.data;
      });
    });
  }

  auto reload = [this] {
    gLoaded = false;
    gLoading = false;
  };

  auto saveCurrent = [this, reload] {
    if (gSelectedId.empty()) return;
    Json body{{"title", gTitle},
              {"data", Json{{"content", Json{{"md", gContent}}}}},
              {"meta", Json::object()}};
    const std::string id = gSelectedId;
    async::run([this, id, body, reload] {
      api::Result res = api_.updateNote(id, body);
      async::post([res, reload] {
        if (res.ok) {
          widgets::toast(widgets::ToastLevel::Success, "Note saved");
          reload();
        } else {
          widgets::toastError("Save failed: " + api::errorMessage(res));
        }
      });
    });
    gDirty = false;
  };

  paneHeader("Notes", "Markdown notes stored on your server", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 230.0f);
    if (widgets::primaryButton("New note", ImVec2(110, 30))) {
      Json body{{"title", "Untitled"},
                {"data", Json{{"content", Json{{"md", ""}}}}},
                {"meta", Json::object()}};
      async::run([this, body, reload] {
        api::Result res = api_.createNote(body);
        async::post([this, res, reload] {
          if (res.ok) {
            gSelectedId = res.data.value("id", "");
            gTitle = "Untitled";
            gContent.clear();
            reload();
          } else {
            widgets::toastError("Could not create note: " + api::errorMessage(res));
          }
        });
      });
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(90, 30))) reload();
  });

  // ---- list
  ImGui::BeginChild("##notelist", ImVec2(300, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(12.0f);
  widgets::searchField("notefilter", &gFilter, "Filter notes", 264.0f);
  ImGui::Dummy(ImVec2(0, 6));
  if (gLoading) widgets::loadingRow("Loading notes...");
  for (const auto& note : noteList) {
    const std::string id = note.value("id", "");
    const std::string title = note.value("title", "Untitled");
    if (!gFilter.empty() && !core::containsNoCase(title, gFilter)) continue;
    ImGui::PushID(id.c_str());
    if (ImGui::Selectable(title.c_str(), id == gSelectedId, 0, ImVec2(264, 26))) {
      gSelectedId = id;
      gTitle = title;
      gContent = noteContent(note);
      gDirty = false;
    }
    if (ImGui::BeginPopupContextItem("notemenu")) {
      if (ImGui::MenuItem("Pin")) {
        async::run([this, id, reload] {
          api_.pinNote(id);
          async::post([reload] { reload(); });
        });
      }
      if (ImGui::MenuItem("Delete")) {
        async::run([this, id, reload] {
          api_.deleteNote(id);
          async::post([this, id, reload] {
            if (gSelectedId == id) gSelectedId.clear();
            reload();
          });
        });
      }
      ImGui::EndPopup();
    }
    widgets::dimText("  %s", core::relativeTime(note.value("updated_at", static_cast<int64_t>(0))).c_str());
    ImGui::PopID();
  }
  if (noteList.empty() && !gLoading) emptyState("No notes yet");
  ImGui::Unindent(12.0f);
  ImGui::EndChild();

  ImGui::SameLine();

  // ---- editor
  ImGui::BeginChild("##noteeditor", ImVec2(0, 0));
  if (gSelectedId.empty()) {
    emptyState("Select a note, or create one");
  } else {
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Indent(16.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220.0f);
    if (ImGui::InputText("##notetitle", &gTitle)) gDirty = true;
    ImGui::SameLine();
    if (ImGui::Button(gPreview ? "Edit" : "Preview", ImVec2(90, 0))) gPreview = !gPreview;
    ImGui::SameLine();
    widgets::beginDisabled(!gDirty);
    if (widgets::primaryButton("Save", ImVec2(90, 0))) saveCurrent();
    widgets::endDisabled(!gDirty);

    ImGui::Dummy(ImVec2(0, 8));
    const ImVec2 size(ImGui::GetContentRegionAvail().x - 18.0f,
                      ImGui::GetContentRegionAvail().y - 20.0f);
    if (gPreview) {
      ImGui::BeginChild("##notepreview", size);
      md::Options options;
      options.wrapWidth = size.x - 20.0f;
      options.imageLoader = [this](const std::string& url) { return fetchImageBytes(url); };
      md::render("notebody", gContent, options);
      ImGui::EndChild();
    } else {
      theme::pushMono();
      if (ImGui::InputTextMultiline("##notebody", &gContent, size)) gDirty = true;
      theme::pop();
    }
    ImGui::Unindent(16.0f);
  }
  ImGui::EndChild();
}

}  // namespace app
