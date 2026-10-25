// Editor bridge in the app: an "editor://" link from the markdown renderer
// resolves against the pinned project folder and opens in the chosen
// editor; the Settings card picks the editor or a custom command; the
// owui://explain link (features.cpp) turns an editor's selection into a chat.
#include <filesystem>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/editorbridge.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

editorbridge::Editor chosenEditor() { return editorbridge::editorFrom(core::Settings::instance().extra.value("editor", std::string("vscode"))); }
std::string customTemplate() { return core::Settings::instance().extra.value("editorCommand", std::string("code --goto {file}:{line}:{col}")); }

std::string gTestNote;

}  // namespace

namespace {
// The pinned project folder: the live one, else the remembered setting.
std::string projectFolderOf(const App& app) {
  if (!app.projectFolder.empty()) return app.projectFolder;
  const Json& extra = core::Settings::instance().extra;
  return extra.contains("projectFolder") && extra["projectFolder"].is_string() ? extra["projectFolder"].get<std::string>() : std::string();
}
}  // namespace

void App::openInEditor(const std::string& link) {
  editorbridge::FileRef ref;
  if (!editorbridge::parseLink(link, &ref)) {
    widgets::toastError("Not a file reference: " + link);
    return;
  }
  const std::string home = native::homeDirectory();
  std::string path = editorbridge::resolvePath(ref.path, projectFolderOf(*this), home);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    // Not in the project: maybe relative to home or to the working folder.
    const std::string alt = editorbridge::resolvePath(ref.path, home, home);
    if (std::filesystem::exists(alt, ec)) {
      path = alt;
    } else {
      widgets::toastError("Can't find " + ref.path + (projectFolderOf(*this).empty() ? " (pin the project folder so relative paths resolve)" : ""));
      return;
    }
  }
  editorbridge::FileRef absolute = ref;
  absolute.path = path;
  const editorbridge::Editor editor = chosenEditor();
  const std::vector<std::string> argv = editorbridge::openCommand(editor, absolute, customTemplate());
  if (argv.empty()) {
    widgets::toastError("The custom editor command is empty - set it under Settings > Editor");
    return;
  }
  const std::string name = editorbridge::editorName(editor);
  const std::string shown = ref.path + ":" + std::to_string(ref.line);
  async::run([argv, home, name, shown] {
    const native::ProcessResult res = native::runProcess(argv, home, 15, false);
    async::post([res, name, shown] {
      if (res.exitCode == 0 && !res.timedOut) {
        widgets::toastInfo("Opened " + shown + " in " + name);
      } else {
        widgets::toastError("Could not open " + shown + " in " + name + (res.output.empty() ? "" : ": " + core::trim(res.output).substr(0, 160)));
      }
    });
  });
}

void App::drawEditorSection(float width) {
  card("editor", [&] {
    ImGui::TextUnformatted("File references in replies (src/app.cpp:120, ~/notes.md:3) open at that line in your editor.");
    const char* names[] = {"VS Code", "Cursor", "Zed", "Xcode", "Sublime Text", "Custom command"};
    const char* ids[] = {"vscode", "cursor", "zed", "xcode", "sublime", "custom"};
    int current = static_cast<int>(chosenEditor());
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Editor", &current, names, 6)) {
      settings().extra["editor"] = ids[current];
      settings().save();
    }
    if (chosenEditor() == editorbridge::Editor::Custom) {
      std::string tmpl = customTemplate();
      ImGui::SetNextItemWidth(std::max(300.0f, width - 300.0f));
      if (ImGui::InputTextWithHint("Command", "code --goto {file}:{line}:{col}", &tmpl)) {
        settings().extra["editorCommand"] = tmpl;
        settings().save();
      }
      widgets::dimText("{file} is quoted for you; {line} and {col} are numbers. Runs with your login shell's PATH not available, so prefer absolute paths.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Test: open settings.json at line 1")) {
      openInEditor("editor://" + core::supportDir() + "/settings.json:1");
    }
    widgets::dimText("Relative paths resolve against the pinned project folder, then your home folder. From an editor, send a selection back with"
                     " owui://explain?file=...&line=...&text=... - see docs/EDITOR-BRIDGE.md for VS Code, Zed and Xcode recipes (Services > Ask Open WebUI works everywhere).");
  }, width);
}

}  // namespace app
