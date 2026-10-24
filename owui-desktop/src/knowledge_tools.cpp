// Knowledge tools: folder -> knowledge base sync, clickable citations, URL
// capture, local embedding index.
//
// STATUS: stubs. Owned by work package D in docs/FEATURE-PLAN.md.
#include "app.h"
#include "ui/widgets.h"

namespace app {

void App::syncFolderToKnowledge(const std::string& folder, const std::string& knowledgeId) {
  (void)folder; (void)knowledgeId;
  widgets::toastInfo("Folder sync: not implemented yet (package D)");
}

void App::stopFolderSync() {}

void App::captureUrlToKnowledge(const std::string& url, const std::string& knowledgeId) {
  (void)url; (void)knowledgeId;
  widgets::toastInfo("URL capture: not implemented yet (package D)");
}

void App::drawCitationPopup() {}

void App::openCitation(const Json& source, int chunkIndex) {
  citationPopup = source;
  citationChunk = chunkIndex;
  showCitation = true;
}

void App::indexFolderLocally(const std::string& folder) {
  (void)folder;
  widgets::toastInfo("Local index: not implemented yet (package D)");
}

std::vector<std::pair<std::string, std::string>> App::localSearch(const std::string& query, int k) {
  (void)query; (void)k;
  return {};
}

void App::drawLocalIndexSection(float width) { (void)width; }

}  // namespace app
