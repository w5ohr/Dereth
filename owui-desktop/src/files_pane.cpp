// Files: everything uploaded to the server, with upload and preview.
#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

bool gLoaded = false;
bool gLoading = false;
std::string gFilter;
std::string gPreviewId;
std::string gPreviewText;
bool gPreviewLoading = false;

}  // namespace

void App::drawFilesPane() {
  auto reload = [] {
    gLoaded = false;
    gLoading = false;
  };

  if (!gLoaded && !gLoading) {
    gLoading = true;
    async::run([this] {
      api::Result res = api_.files();
      async::post([this, res] {
        gLoading = false;
        gLoaded = true;
        if (res.ok && res.data.is_array()) fileList = res.data;
      });
    });
  }

  paneHeader("Files", "Documents you have uploaded for retrieval", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 240.0f);
    if (widgets::primaryButton("Upload", ImVec2(110, 30))) {
      for (const std::string& path : platform::openFiles(true)) {
        bool ok = false;
        const std::string bytes = core::readFile(path, &ok);
        if (!ok) continue;
        const std::string name = core::baseName(path);
        const std::string mime = core::mimeForExtension(core::extension(path));
        async::run([this, name, mime, bytes, reload] {
          api::Result res = api_.uploadFile(name, mime, bytes);
          async::post([res, name, reload] {
            if (res.ok) {
              widgets::toast(widgets::ToastLevel::Success, "Uploaded " + name);
              reload();
            } else {
              widgets::toastError("Upload failed: " + api::errorMessage(res));
            }
          });
        });
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(90, 30))) reload();
  });

  paneBody("##files");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;
  widgets::searchField("filefilter", &gFilter, "Filter files", 320.0f);
  ImGui::Dummy(ImVec2(0, 8));

  if (gLoading) widgets::loadingRow("Loading files...");
  if (fileList.empty() && !gLoading) emptyState("Nothing uploaded yet");

  if (ImGui::BeginTable("##filetable", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(width, 0))) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.18f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.12f);
    ImGui::TableSetupColumn("Added", ImGuiTableColumnFlags_WidthStretch, 0.18f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.18f);
    ImGui::TableHeadersRow();

    for (const auto& file : fileList) {
      const std::string id = file.value("id", "");
      const std::string name = file.value("filename", "file");
      if (!gFilter.empty() && !core::containsNoCase(name, gFilter)) continue;
      const Json& meta = file.value("meta", Json::object());
      ImGui::PushID(id.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(name.c_str());
      ImGui::TableSetColumnIndex(1);
      widgets::dimText("%s", meta.value("content_type", "").c_str());
      ImGui::TableSetColumnIndex(2);
      widgets::dimText("%s", core::humanBytes(meta.value("size", static_cast<int64_t>(0))).c_str());
      ImGui::TableSetColumnIndex(3);
      widgets::dimText("%s",
                       core::relativeTime(file.value("created_at", static_cast<int64_t>(0))).c_str());
      ImGui::TableSetColumnIndex(4);
      if (ImGui::SmallButton("Preview")) {
        gPreviewId = id;
        gPreviewText.clear();
        gPreviewLoading = true;
        async::run([this, id] {
          std::string bytes;
          api_.fetchBinary("/api/v1/files/" + id + "/content", &bytes);
          async::post([bytes] {
            gPreviewLoading = false;
            gPreviewText = bytes.substr(0, 20000);
          });
        });
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Save as...")) {
        const std::string path = platform::saveFile(name);
        if (!path.empty()) {
          async::run([this, id, path] {
            std::string bytes;
            if (api_.fetchBinary("/api/v1/files/" + id + "/content", &bytes)) {
              core::writeFile(path, bytes);
              async::post([path] {
                widgets::toast(widgets::ToastLevel::Success, "Saved to " + path);
              });
            }
          });
        }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) {
        async::run([this, id, reload] {
          api_.deleteFile(id);
          async::post([reload] { reload(); });
        });
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (!gPreviewId.empty()) {
    ImGui::Dummy(ImVec2(0, 12));
    card("preview", [&] {
      ImGui::TextUnformatted("Preview");
      ImGui::SameLine(width - 90.0f);
      if (ImGui::SmallButton("Close")) {
        gPreviewId.clear();
        gPreviewText.clear();
      }
      if (gPreviewLoading) {
        widgets::loadingRow("Fetching...");
      } else {
        ImGui::BeginChild("##previewbody", ImVec2(width - 40.0f, 260.0f));
        theme::pushMono();
        ImGui::PushTextWrapPos(width - 60.0f);
        ImGui::TextUnformatted(gPreviewText.c_str());
        ImGui::PopTextWrapPos();
        theme::pop();
        ImGui::EndChild();
      }
    }, width);
  }

  paneEnd();
}

}  // namespace app
