// Code tools: run/lint code blocks, apply a block to a file, folder and git
// context, build-error explainer, command runner, project mode.
//
// STATUS: stubs. Owned by work package C in docs/FEATURE-PLAN.md. Each
// function below is declared in app.h and wired from chat_pane.cpp /
// tools_pane.cpp; replace the body, keep the signature.
#include "app.h"
#include "native.h"
#include "ui/widgets.h"

namespace app {

void App::runCodeBlock(const std::string& code, const std::string& language) {
  (void)code; (void)language;
  widgets::toastInfo("Run code: not implemented yet (package C)");
}

void App::lintCodeBlock(const std::string& code, const std::string& language) {
  (void)code; (void)language;
  widgets::toastInfo("Lint: not implemented yet (package C)");
}

void App::applyCodeBlockToFile(const std::string& code) {
  (void)code;
  widgets::toastInfo("Apply to file: not implemented yet (package C)");
}

void App::attachFolderAsContext(const std::string& folder) {
  (void)folder;
  widgets::toastInfo("Attach folder: not implemented yet (package C)");
}

void App::attachGitContext(const std::string& folder, const std::string& mode) {
  (void)folder; (void)mode;
  widgets::toastInfo("Git context: not implemented yet (package C)");
}

void App::explainBuildError(const std::string& log) {
  (void)log;
  widgets::toastInfo("Explain build error: not implemented yet (package C)");
}

void App::setProjectFolder(const std::string& folder) {
  projectFolder = folder;
  settings().extra["projectFolder"] = folder;
  settings().save();
}

std::string App::projectContextBlock() const { return ""; }

void App::drawProjectFilesPopup() {}

void App::runCommand(const std::string& command) {
  (void)command;
  widgets::toastInfo("Command runner: not implemented yet (package C)");
}

CodeResult* App::codeResultFor(const std::string& key) {
  auto it = codeResults.find(key);
  return it == codeResults.end() ? nullptr : &it->second;
}

void App::drawCodeBlockActions(const std::string& code, const std::string& language) {
  (void)code; (void)language;
}

}  // namespace app
