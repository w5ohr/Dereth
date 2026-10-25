// The document column: a PDFKit view beside the chat showing the file a
// citation came from, scrolled to the page it names with the cited passage
// highlighted. Opened from openCitation() for PDF sources; other sources
// keep the excerpt popup.
#include "app.h"

#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {

void App::openDocument(const std::string& fileId, const std::string& name, int page,
                       const std::string& passage) {
  showDocument = true;
  showArtifacts = false;
  documentName = name;
  documentPage = page;
  documentPassage = passage;
  documentError.clear();
  documentFoundPage = 0;
  pane = Pane::Chat;
  native::pdfInstall();

  if (fileId == documentFileId && !documentLoading) {
    // Same file: just move the highlight.
    documentFoundPage = native::pdfHighlight(passage, page);
    return;
  }
  documentFileId = fileId;
  documentLoading = true;
  const std::string path = core::cacheDir() + "/doc-" + fileId + ".pdf";
  async::run([this, fileId, path, page, passage] {
    bool ok = false;
    std::string bytes = core::readFile(path, &ok);
    std::string error;
    if (!ok || bytes.empty()) {
      bytes.clear();
      if (!api_.fetchBinary("/api/v1/files/" + fileId + "/content", &bytes) || bytes.empty()) {
        error = "Could not download the file";
      } else {
        core::writeFile(path, bytes);
      }
    }
    async::post([this, fileId, path, bytes, error, page, passage] {
      if (documentFileId != fileId) return;   // another document was opened meanwhile
      documentLoading = false;
      if (!error.empty()) {
        documentError = error;
        return;
      }
      if (!native::pdfLoad(bytes)) {
        documentError = "PDFKit could not read this file";
        return;
      }
      documentPath = path;
      documentFoundPage = native::pdfHighlight(passage, page);
    });
  });
}

void App::drawDocumentColumn(float width) {
  const auto& palette = theme::palette();
  ImGui::BeginChild("##document", ImVec2(width, 0), ImGuiChildFlags_Borders);
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::Indent(10.0f);

  theme::pushBold();
  ImGui::PushTextWrapPos(width - 80.0f);
  ImGui::TextUnformatted(documentName.empty() ? "Document" : documentName.c_str());
  ImGui::PopTextWrapPos();
  theme::pop();
  ImGui::SameLine(width - 62.0f);
  if (ImGui::SmallButton("Close")) {
    showDocument = false;
    native::pdfClearHighlights();
  }

  const int pages = native::pdfPageCount();
  const int current = native::pdfCurrentPage();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  widgets::beginDisabled(pages == 0 || current <= 1);
  if (ImGui::SmallButton("<")) native::pdfGoToPage(current - 1);
  widgets::endDisabled(pages == 0 || current <= 1);
  ImGui::SameLine();
  if (pages > 0) ImGui::Text("page %d / %d", current, pages);
  else ImGui::TextUnformatted(documentLoading ? "loading..." : "no pages");
  ImGui::SameLine();
  widgets::beginDisabled(pages == 0 || current >= pages);
  if (ImGui::SmallButton(">")) native::pdfGoToPage(current + 1);
  widgets::endDisabled(pages == 0 || current >= pages);
  ImGui::SameLine();
  if (widgets::ghostButton("Find passage again")) documentFoundPage = native::pdfHighlight(documentPassage, documentPage);
  ImGui::SameLine();
  if (widgets::ghostButton("Excerpt")) showCitation = true;
  ImGui::SameLine();
  widgets::beginDisabled(documentPath.empty());
  if (widgets::ghostButton("Open in Preview")) platform::openUrl("file://" + documentPath);
  widgets::endDisabled(documentPath.empty());
  ImGui::PopStyleVar();

  if (!documentError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
    ImGui::TextUnformatted(documentError.c_str());
    ImGui::PopStyleColor();
  } else if (documentLoading) {
    widgets::loadingRow("Fetching the document...");
  } else if (documentFoundPage > 0) {
    widgets::dimText("cited passage highlighted on page %d", documentFoundPage);
  } else if (!documentPassage.empty() && pages > 0) {
    widgets::dimText("passage not matched in the PDF text%s",
                     documentPage > 0 ? (" - showing page " + std::to_string(documentPage)).c_str() : "");
  }
  ImGui::Unindent(10.0f);
  ImGui::Separator();

  // The PDF view fills what is left.
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float viewWidth = std::max(avail.x, 1.0f);
  const float viewHeight = std::max(avail.y - 4.0f, 80.0f);
  native::pdfInstall();
  native::pdfSetFrame(pos.x, pos.y, viewWidth, viewHeight);
  documentShownThisFrame = !documentLoading && documentError.empty() && pages > 0;
  ImGui::Dummy(ImVec2(viewWidth, viewHeight));
  ImGui::EndChild();
}

}  // namespace app
