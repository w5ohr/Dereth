// Script tools in the app: the folder (support/scripts), the cached scan,
// the built-in "scripts" tool server the model sees, running a tool through
// native::runProcess, and the card in Tools > MCP that lists the scripts,
// writes the examples and tries one.
#include <sys/stat.h>

#include <algorithm>
#include <ctime>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/scripttools.h"
#include "core/settings.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr const char* kServerId = "builtin:scripts";
constexpr const char* kServerName = "scripts";
constexpr size_t kMaxOutput = 24000;

std::string scriptsDir() { return core::supportDir() + "/scripts"; }

std::mutex gMutex;
std::vector<scripttools::Tool> gTools;
double gScannedAt = -1e9;

// Rescans at most every couple of seconds: the folder is small and the
// model's turn, the card and the spec list all ask.
std::vector<scripttools::Tool> tools(bool force = false) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (force || core::nowMillis() - gScannedAt > 2000.0) {
    gTools = scripttools::scan(scriptsDir());
    gScannedAt = core::nowMillis();
  }
  return gTools;
}

bool enabled() { return core::Settings::instance().extra.value("scriptTools", true); }

std::string tryArgs;
std::string tryOutput;
std::string tryTool;
bool tryRunning = false;

}  // namespace

std::vector<agents::ToolSpec> App::scriptToolSpecs() const {
  std::vector<agents::ToolSpec> out;
  if (!enabled()) return out;
  for (const scripttools::Tool& t : tools()) {
    if (!t.error.empty()) continue;
    agents::ToolSpec s;
    s.name = std::string(kServerName) + "__" + t.name;
    s.description = t.description + " (user script " + t.file + (t.sandbox ? ", sandboxed: no network, writes only in the working folder)" : ", unsandboxed)");
    s.parameters = t.schema();
    s.serverId = kServerId;
    s.serverName = kServerName;
    s.tool = t.name;
    out.push_back(s);
  }
  return out;
}

bool App::isScriptToolServer(const std::string& serverId) { return serverId == kServerId; }

agents::ToolOutcome App::callScriptTool(const std::string& name, const Json& args, const std::string& projectFolder) {
  agents::ToolOutcome out;
  scripttools::Tool tool;
  bool found = false;
  for (const scripttools::Tool& t : tools(true)) {
    if (t.name == name && t.error.empty()) {
      tool = t;
      found = true;
      break;
    }
  }
  if (!found) {
    out.error = "script tool '" + name + "' is not available (removed or its header is broken)";
    return out;
  }
  const std::string why = scripttools::validateArgs(tool, args);
  if (!why.empty()) {
    out.isError = true;
    out.text = why;
    return out;
  }
  // Sandboxed runs may write only inside the working folder: the pinned
  // project when there is one, else a scratch folder of their own.
  std::string workdir = projectFolder;
  if (workdir.empty()) {
    workdir = tool.sandbox ? core::cacheDir() + "/script-run" : native::homeDirectory();
    if (tool.sandbox) mkdir(workdir.c_str(), 0700);
  }
  const double started = core::nowMillis();
  const native::ProcessResult res = native::runProcess(scripttools::argv(tool, args), workdir, tool.timeoutSec, tool.sandbox);
  out.durationMs = core::nowMillis() - started;
  std::string text = res.output;
  if (text.size() > kMaxOutput) text = text.substr(0, kMaxOutput) + "\n...[truncated " + std::to_string(res.output.size() - kMaxOutput) + " chars]";
  if (res.timedOut) {
    out.isError = true;
    out.text = "timed out after " + std::to_string(tool.timeoutSec) + " s" + (text.empty() ? "" : "; output so far:\n" + text);
    return out;
  }
  if (res.exitCode != 0) {
    out.isError = true;
    out.text = "exit status " + std::to_string(res.exitCode) + (text.empty() ? "" : ":\n" + text);
    return out;
  }
  out.text = core::trim(text).empty() ? "(the script printed nothing)" : text;
  return out;
}

void App::drawScriptToolsCard(float width) {
  const auto& palette = theme::palette();
  card("builtin-scripts", [&] {
    theme::pushBold();
    ImGui::TextUnformatted("Script tools (built in)");
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("your own shell / Python / AppleScript / JXA scripts as tools");
    bool on = enabled();
    if (ImGui::Checkbox("Offer the scripts in this folder to the model (with MCP tools on)", &on)) {
      settings().extra["scriptTools"] = on;
      settings().save();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 40.0f);
    widgets::dimText("%s", scriptsDir().c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton("Reveal in Finder")) {
      mkdir(scriptsDir().c_str(), 0700);
      platform::revealInFinder(scriptsDir());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) tools(true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Write the examples")) {
      mkdir(scriptsDir().c_str(), 0700);
      int written = 0;
      for (const auto& [file, content] : scripttools::examples()) {
        const std::string path = scriptsDir() + "/" + file;
        bool exists = false;
        core::readFile(path, &exists);
        if (exists) continue;
        core::writeFile(path, content);
        chmod(path.c_str(), 0700);
        ++written;
      }
      tools(true);
      widgets::toast(widgets::ToastLevel::Success, written ? "Wrote " + std::to_string(written) + " example script" + (written == 1 ? "" : "s") : "The examples are already there");
    }
    widgets::dimText("A file becomes a tool with a comment header: owui-tool: name / description: ... / param: name type [required] - what it is / timeout: 60 / sandbox: on|off.");
    widgets::dimText("Arguments reach the script as JSON (last argument and $OWUI_ARGS) and as $OWUI_ARG_NAME; stdout is the result. Sandboxed scripts get no network and write only inside the working folder.");
    ImGui::Dummy(ImVec2(0, 4));

    const std::vector<scripttools::Tool> list = tools();
    if (list.empty()) widgets::dimText("No scripts yet - write the examples or drop your own in the folder.");
    for (const scripttools::Tool& t : list) {
      ImGui::PushID(t.path.c_str());
      theme::pushBold();
      ImGui::TextUnformatted(t.name.c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::dimText("%s  %s%s", t.file.c_str(), t.runner.empty() ? "exec" : core::join(t.runner, " ").c_str(), t.sandbox ? "  sandboxed" : "  UNSANDBOXED");
      if (!t.error.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
        ImGui::TextUnformatted(t.error.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::SameLine();
        if (ImGui::SmallButton("Try")) {
          tryTool = t.name;
          tryOutput.clear();
          Json seed = Json::object();
          for (const scripttools::Param& p : t.params) {
            if (p.required) seed[p.name] = p.type == "integer" || p.type == "number" ? Json(1) : p.type == "boolean" ? Json(true) : Json("example");
          }
          tryArgs = seed.dump();
        }
      }
      std::string params;
      for (const scripttools::Param& p : t.params) {
        params += (params.empty() ? "" : ", ") + p.name + ": " + p.type + (p.required ? " (required)" : "");
      }
      widgets::dimText("    %s%s%s", t.description.c_str(), params.empty() ? "" : "  -  ", params.c_str());
      ImGui::PopID();
    }

    if (!tryTool.empty()) {
      ImGui::Separator();
      ImGui::Text("Try %s with arguments:", tryTool.c_str());
      ImGui::SetNextItemWidth(std::max(200.0f, width - 220.0f));
      ImGui::InputText("##tryargs", &tryArgs);
      ImGui::SameLine();
      if (ImGui::Button(tryRunning ? "Running..." : "Run") && !tryRunning) {
        Json args = Json::parse(tryArgs, nullptr, false);
        if (args.is_discarded()) {
          tryOutput = "Arguments are not valid JSON";
        } else {
          tryRunning = true;
          const std::string name = tryTool;
          const std::string folder = projectFolder;
          async::run([this, name, args, folder] {
            const agents::ToolOutcome r = callScriptTool(name, args, folder);
            async::post([r] {
              tryRunning = false;
              tryOutput = !r.error.empty() ? "Error: " + r.error : (r.isError ? "Script error: " : "") + r.text +
                                                                       "\n[" + std::to_string(static_cast<int>(r.durationMs)) + " ms]";
            });
          });
        }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Close")) {
        tryTool.clear();
        tryOutput.clear();
      }
      if (!tryOutput.empty()) {
        ImGui::BeginChild("##tryscriptout", ImVec2(width - 40.0f, 140.0f), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(tryOutput.substr(0, 4000).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
      }
    }
  }, width);
}

}  // namespace app
