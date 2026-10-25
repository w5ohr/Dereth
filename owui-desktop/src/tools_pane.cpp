// Tools pane: command runner, build-error explainer, benchmark panel, local
// index, folder sync, URL capture.
//
// The heavy lifting lives elsewhere (code_tools.cpp for commands and build
// errors, knowledge_tools.cpp for sync, capture and the local index); this
// file is the UI over it plus the benchmark runner, which is self-contained.
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <sstream>

#include "core/async.h"
#include "core/common.h"
#include "native.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

enum class Tab { Command, BuildError, Benchmark, LocalIndex, FolderSync, UrlCapture, Mcp, Terminal, Plugins, Git, Usage, Backup, Recipes, TestLoop, Audit, Traffic, Health, Models };

Tab gTab = Tab::Command;

// Command tab: autoscroll only when new output arrives, so the user can
// scroll back through a long listing without being yanked to the bottom.
size_t gCommandSeen = 0;

// Benchmark tab.
std::set<std::string> gBenchModels;
double gBenchStarted = 0.0;
double gBenchElapsed = 0.0;
int gBenchExit = 0;
bool gBenchTimedOut = false;
constexpr int kBenchTimeoutSec = 20 * 60;

// Knowledge-base pickers.
bool gKnowledgeRequested = false;
std::string gSyncFolder;
std::string gSyncKnowledge;
std::string gCaptureUrl;
std::string gCaptureKnowledge;

const char* tabName(Tab tab) {
  switch (tab) {
    case Tab::Command: return "Command";
    case Tab::BuildError: return "Build error";
    case Tab::Benchmark: return "Benchmark";
    case Tab::LocalIndex: return "Local index";
    case Tab::FolderSync: return "Folder sync";
    case Tab::UrlCapture: return "URL capture";
    case Tab::Mcp: return "MCP";
    case Tab::Terminal: return "Terminal";
    case Tab::Plugins: return "Plugins";
    case Tab::Git: return "Git";
    case Tab::Usage: return "Usage";
    case Tab::Backup: return "Backup";
    case Tab::Recipes: return "Recipes";
    case Tab::TestLoop: return "Test loop";
    case Tab::Audit: return "Audit";
    case Tab::Traffic: return "Traffic";
    case Tab::Health: return "Health";
    case Tab::Models: return "Models";
  }
  return "";
}

// Parses bench_coding.py output. Per-task lines look like
//   "  py_bucket    PASS    3.6s   80.2 tok/s"
// under a "=== model ===" banner, and the summary table rows like
//   "qwen3:8b                      5/     6       120      55.2".
// Summary rows are matched against the requested model names rather than
// split on whitespace, because the script pads the name column to 30
// characters and a longer name runs straight into the score.
Json parseBenchOutput(const std::string& output, const std::vector<std::string>& models) {
  Json results = Json::object();
  for (const auto& model : models) {
    results[model] = {{"passed", 0}, {"of", 0}, {"seconds", 0.0}, {"rate", 0.0},
                      {"tasks", Json::array()}, {"summary", false}};
  }
  static const std::regex banner(R"(^=== (.+) ===\s*$)");
  static const std::regex task(
      R"(^\s+(\S+)\s+(PASS|FAIL)\s+([\d.]+)s\s+([\d.]+) tok/s\s*(.*)$)");
  static const std::regex error(R"(^\s+(\S+)\s+ERROR\s+(.*)$)");
  static const std::regex summary(R"(^\s*(\d+)/\s*(\d+)\s+([\d.]+)\s+([\d.]+)\s*$)");

  std::string current;
  bool inSummary = false;
  std::istringstream lines(output);
  std::string line;
  std::smatch m;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (core::startsWith(line, "=====")) {
      inSummary = true;
      continue;
    }
    if (inSummary) {
      for (const auto& model : models) {
        if (!core::startsWith(line, model)) continue;
        const std::string rest = line.substr(model.size());
        if (!std::regex_match(rest, m, summary)) continue;
        Json& row = results[model];
        row["passed"] = std::stoi(m[1]);
        row["of"] = std::stoi(m[2]);
        row["seconds"] = std::stod(m[3]);
        row["rate"] = std::stod(m[4]);
        row["summary"] = true;
        break;
      }
      continue;
    }
    if (std::regex_match(line, m, banner)) {
      current = m[1];
      if (!results.contains(current)) {
        results[current] = {{"passed", 0}, {"of", 0}, {"seconds", 0.0}, {"rate", 0.0},
                            {"tasks", Json::array()}, {"summary", false}};
      }
      continue;
    }
    if (current.empty()) continue;
    if (std::regex_match(line, m, task)) {
      results[current]["tasks"].push_back({{"id", m[1].str()},
                                           {"status", m[2].str() == "PASS" ? "pass" : "fail"},
                                           {"seconds", std::stod(m[3])},
                                           {"rate", std::stod(m[4])},
                                           {"note", core::trim(m[5].str())}});
    } else if (std::regex_match(line, m, error)) {
      results[current]["tasks"].push_back(
          {{"id", m[1].str()}, {"status", "error"}, {"seconds", 0.0}, {"rate", 0.0},
           {"note", core::trim(m[2].str())}});
    }
  }

  // A run that died before the summary (timeout, crash) still has its task
  // lines; derive the score and an average rate from those.
  for (auto it = results.begin(); it != results.end(); ++it) {
    Json& row = it.value();
    if (row.value("summary", false)) continue;
    int passed = 0;
    double seconds = 0.0, rateSum = 0.0;
    int rated = 0;
    for (const auto& t : row["tasks"]) {
      if (t.value("status", "") == "pass") ++passed;
      seconds += t.value("seconds", 0.0);
      if (t.value("rate", 0.0) > 0.0) {
        rateSum += t.value("rate", 0.0);
        ++rated;
      }
    }
    row["passed"] = passed;
    row["of"] = static_cast<int>(row["tasks"].size());
    row["seconds"] = seconds;
    row["rate"] = rated ? rateSum / rated : 0.0;
  }
  return results;
}

// One labelled horizontal bar: track, fill proportional to fraction, value
// text at the right end.
void drawBar(const char* label, float fraction, const std::string& value, ImU32 color,
             float width) {
  const auto& p = theme::palette();
  const float labelWidth = 210.0f;
  const float valueWidth = 110.0f;
  const float barHeight = 16.0f;
  const float trackWidth = std::max(80.0f, width - labelWidth - valueWidth - 24.0f);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float rowHeight = std::max(barHeight, ImGui::GetTextLineHeight()) + 6.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const float textY = start.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
  const ImVec2 track(start.x + labelWidth, start.y + (rowHeight - barHeight) * 0.5f);
  dl->AddRectFilled(track, ImVec2(track.x + trackWidth, track.y + barHeight), p.panelAlt, 4.0f);
  const float fill = trackWidth * std::clamp(fraction, 0.0f, 1.0f);
  if (fill > 0.5f) {
    dl->AddRectFilled(track, ImVec2(track.x + fill, track.y + barHeight), color, 4.0f);
  }
  dl->AddText(ImVec2(start.x, textY), p.text, label);
  dl->AddText(ImVec2(track.x + trackWidth + 10.0f, textY), p.text, value.c_str());
  ImGui::SetCursorScreenPos(start);
  ImGui::Dummy(ImVec2(width, rowHeight));
}

// Knowledge-base dropdown over App::knowledgeList, writing the chosen id.
void knowledgeCombo(const char* id, const Json& list, std::string* selected, float width) {
  std::string preview = "Choose a knowledge base";
  for (const auto& kb : list) {
    if (kb.value("id", "") == *selected) preview = kb.value("name", *selected);
  }
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, preview.c_str())) {
    if (list.empty()) widgets::dimText("No knowledge bases on this server");
    for (const auto& kb : list) {
      const std::string kbId = kb.value("id", "");
      const bool active = kbId == *selected;
      if (ImGui::Selectable(kb.value("name", kbId).c_str(), active)) *selected = kbId;
      if (active) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}

}  // namespace

void App::drawToolsPane() {
  paneHeader("Tools", "Command runner, build errors, benchmark, local index, MCP servers, terminal, plugins, git, usage, backup, recipes, test loop, audit, traffic", nullptr);
  paneBody("##tools");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;
  const auto& palette = theme::palette();

  if (ImGui::BeginTabBar("##toolstabs")) {
    for (Tab tab : {Tab::Command, Tab::BuildError, Tab::Benchmark, Tab::LocalIndex,
                    Tab::FolderSync, Tab::UrlCapture, Tab::Mcp, Tab::Terminal, Tab::Plugins, Tab::Git, Tab::Usage, Tab::Backup, Tab::Recipes, Tab::TestLoop, Tab::Audit, Tab::Traffic, Tab::Health, Tab::Models}) {
      if (ImGui::BeginTabItem(tabName(tab))) {
        gTab = tab;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  ImGui::Dummy(ImVec2(0, 8));

  // Both knowledge tabs need the list; the workspace pane fills it lazily
  // too, so only fetch when nobody has yet.
  if ((gTab == Tab::FolderSync || gTab == Tab::UrlCapture) && knowledgeList.empty() &&
      !gKnowledgeRequested) {
    gKnowledgeRequested = true;
    async::run([this] {
      api::Result res = api_.knowledgeList();
      async::post([this, res] {
        if (!res.ok) return;
        if (res.data.is_array()) knowledgeList = res.data;
        else if (res.data.contains("items")) knowledgeList = res.data["items"];
      });
    });
  }

  switch (gTab) {
    case Tab::Command: {
      card("cmdfolder", [&] {
        ImGui::TextUnformatted("Working folder");
        ImGui::SameLine(140.0f);
        if (projectFolder.empty()) {
          widgets::dimText("%s (home - no project folder set)", native::homeDirectory().c_str());
        } else {
          theme::pushMono();
          ImGui::TextUnformatted(projectFolder.c_str());
          theme::pop();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Choose...")) {
          const auto picked = native::chooseFolder();
          if (!picked.empty()) setProjectFolder(picked.front());
        }
      }, width);

      bool run = false;
      ImGui::SetNextItemWidth(width - 230.0f);
      theme::pushMono();
      widgets::beginDisabled(commandRunning);
      if (ImGui::InputTextWithHint("##cmd", "e.g. ls -la, git status, make", &commandInput,
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
        run = true;
      }
      widgets::endDisabled(commandRunning);
      theme::pop();
      ImGui::SameLine();
      widgets::beginDisabled(commandRunning || core::trim(commandInput).empty());
      if (widgets::primaryButton("Run", ImVec2(100, 0))) run = true;
      widgets::endDisabled(commandRunning || core::trim(commandInput).empty());
      ImGui::SameLine();
      if (ImGui::Button("Clear", ImVec2(100, 0))) {
        commandOutput.clear();
        gCommandSeen = 0;
      }
      if (run && !commandRunning && !core::trim(commandInput).empty()) {
        runCommand(core::trim(commandInput));
      }

      if (commandRunning) {
        widgets::spinner("##cmdspin", 7.0f, 2.0f);
        ImGui::SameLine();
        widgets::dimText("running...");
      }

      ImGui::Dummy(ImVec2(0, 4));
      const float outputHeight = std::max(200.0f, ImGui::GetContentRegionAvail().y - 60.0f);
      const bool grew = commandOutput.size() != gCommandSeen;
      gCommandSeen = commandOutput.size();
      monoOutput("##cmdout", commandOutput, ImVec2(width, outputHeight), grew);

      ImGui::Dummy(ImVec2(0, 4));
      widgets::beginDisabled(commandOutput.empty());
      if (ImGui::Button("Send output to chat", ImVec2(200, 0))) {
        std::string block = "```\n" + commandOutput;
        if (!block.empty() && block.back() != '\n') block += "\n";
        block += "```\n";
        composer = composer.empty() ? block : composer + "\n" + block;
        ++composerGeneration;
        focusComposer = true;
        pane = Pane::Chat;
      }
      widgets::endDisabled(commandOutput.empty());
      break;
    }

    case Tab::BuildError: {
      widgets::dimText(
          "Paste compiler or test output. File references are resolved against the project "
          "folder and the surrounding lines go into the prompt.");
      ImGui::Dummy(ImVec2(0, 4));
      theme::pushMono();
      ImGui::InputTextMultiline("##builderr", &buildErrorInput,
                                ImVec2(width, std::max(220.0f,
                                                       ImGui::GetContentRegionAvail().y - 60.0f)));
      theme::pop();
      ImGui::Dummy(ImVec2(0, 4));
      widgets::beginDisabled(core::trim(buildErrorInput).empty());
      if (widgets::primaryButton("Explain", ImVec2(120, 0))) {
        explainBuildError(buildErrorInput);
        pane = Pane::Chat;
      }
      widgets::endDisabled(core::trim(buildErrorInput).empty());
      ImGui::SameLine();
      if (ImGui::Button("Clear", ImVec2(100, 0))) buildErrorInput.clear();
      break;
    }

    case Tab::Benchmark: {
      card("benchpick", [&] {
        ImGui::TextUnformatted("Models");
        widgets::dimText("bench_coding.py talks to Ollama directly, so only Ollama models "
                         "are listed.");
        ImGui::Dummy(ImVec2(0, 2));
        int shown = 0;
        for (const auto& model : models) {
          if (model.owner != "ollama") continue;
          bool on = gBenchModels.count(model.id) > 0;
          if (shown % 3 != 0) ImGui::SameLine(12.0f + (shown % 3) * (width / 3.0f));
          if (ImGui::Checkbox(model.id.c_str(), &on)) {
            if (on) gBenchModels.insert(model.id);
            else gBenchModels.erase(model.id);
          }
          ++shown;
        }
        if (shown == 0) widgets::dimText("No Ollama models reported by the server");
      }, width);

      const bool canRun = !benchRunning && !gBenchModels.empty();
      widgets::beginDisabled(!canRun);
      if (widgets::primaryButton("Run benchmark", ImVec2(160, 0))) {
        // Keep the order the user sees in the list rather than set order.
        std::vector<std::string> chosen;
        for (const auto& model : models) {
          if (gBenchModels.count(model.id)) chosen.push_back(model.id);
        }
        benchRunning = true;
        benchOutput.clear();
        benchResults = Json::object();
        gBenchStarted = core::nowMillis();
        gBenchTimedOut = false;
        const std::string script = native::homeDirectory() + "/.ai-server/bench/bench_coding.py";
        async::runDetached([chosen, script] {
          native::ProcessResult res = native::runProcess(
              {"python3", script, "-m", core::join(chosen, ",")}, native::homeDirectory(),
              kBenchTimeoutSec, false);
          async::post([chosen, res] {
            App& app = App::instance();
            app.benchRunning = false;
            app.benchOutput = res.output;
            app.benchResults = parseBenchOutput(res.output, chosen);
            gBenchExit = res.exitCode;
            gBenchTimedOut = res.timedOut;
            gBenchElapsed = (core::nowMillis() - gBenchStarted) / 1000.0;
            if (res.timedOut) {
              widgets::toast(widgets::ToastLevel::Warning, "Benchmark stopped at 20 minutes");
            } else if (res.exitCode != 0) {
              widgets::toastError("Benchmark exited with code " + std::to_string(res.exitCode));
            } else {
              widgets::toast(widgets::ToastLevel::Success, "Benchmark finished");
            }
          });
        });
      }
      widgets::endDisabled(!canRun);
      ImGui::SameLine();
      if (benchRunning) {
        widgets::spinner("##benchspin", 7.0f, 2.0f);
        ImGui::SameLine();
        const int secs = static_cast<int>((core::nowMillis() - gBenchStarted) / 1000.0);
        widgets::dimText("running %d:%02d - output appears when the run ends (limit 20:00)",
                         secs / 60, secs % 60);
      } else if (!benchResults.empty()) {
        widgets::dimText("finished in %.0fs, exit %d%s", gBenchElapsed, gBenchExit,
                         gBenchTimedOut ? ", timed out" : "");
      } else {
        widgets::dimText("%zu selected", gBenchModels.size());
      }

      if (!benchResults.empty()) {
        ImGui::Dummy(ImVec2(0, 6));
        double maxRate = 0.0;
        for (auto it = benchResults.begin(); it != benchResults.end(); ++it) {
          maxRate = std::max(maxRate, it.value().value("rate", 0.0));
        }
        card("benchscore", [&] {
          widgets::sectionHeader("SCORE (tasks passed)");
          for (auto it = benchResults.begin(); it != benchResults.end(); ++it) {
            const int passed = it.value().value("passed", 0);
            const int of = it.value().value("of", 0);
            const float fraction = of > 0 ? static_cast<float>(passed) / of : 0.0f;
            const ImU32 color = fraction >= 0.99f   ? palette.success
                                : fraction >= 0.5f ? palette.accent
                                                   : palette.warning;
            drawBar(it.key().c_str(), fraction,
                    std::to_string(passed) + " / " + std::to_string(of), color, width - 30.0f);
          }
          ImGui::Dummy(ImVec2(0, 6));
          widgets::sectionHeader("SPEED (tokens per second)");
          for (auto it = benchResults.begin(); it != benchResults.end(); ++it) {
            const double rate = it.value().value("rate", 0.0);
            char value[32];
            snprintf(value, sizeof(value), "%.1f tok/s", rate);
            drawBar(it.key().c_str(), maxRate > 0 ? static_cast<float>(rate / maxRate) : 0.0f,
                    value, palette.accent, width - 30.0f);
          }
        }, width);

        card("benchtasks", [&] {
          widgets::sectionHeader("PER TASK");
          if (ImGui::BeginTable("##benchtasks", 5,
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_SizingStretchProp,
                                ImVec2(width - 30.0f, 0))) {
            ImGui::TableSetupColumn("Model");
            ImGui::TableSetupColumn("Task");
            ImGui::TableSetupColumn("Result");
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("tok/s");
            ImGui::TableHeadersRow();
            for (auto it = benchResults.begin(); it != benchResults.end(); ++it) {
              for (const auto& t : it.value().value("tasks", Json::array())) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                widgets::dimText("%s", it.key().c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(t.value("id", "").c_str());
                ImGui::TableSetColumnIndex(2);
                const std::string status = t.value("status", "");
                widgets::pill(status, status == "pass"   ? palette.success
                                      : status == "fail" ? palette.warning
                                                         : palette.danger);
                if (!t.value("note", "").empty() && ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("%s", t.value("note", "").c_str());
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1fs", t.value("seconds", 0.0));
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1f", t.value("rate", 0.0));
              }
            }
            ImGui::EndTable();
          }
        }, width);
      }

      if (!benchOutput.empty()) {
        widgets::sectionHeader("RAW OUTPUT");
        monoOutput("##benchraw", benchOutput, ImVec2(width, 260.0f), false);
      }
      break;
    }

    case Tab::LocalIndex: {
      drawLocalIndexSection(width);
      drawChatIndexCard(width);
      break;
    }

    case Tab::FolderSync: {
      card("sync", [&] {
        ImGui::TextUnformatted("Keep a folder mirrored into a knowledge base");
        widgets::dimText("Files are uploaded once, then re-uploaded whenever they change on disk.");
        ImGui::Dummy(ImVec2(0, 4));
        if (gSyncFolder.empty() && !folderSyncPath.empty()) gSyncFolder = folderSyncPath;
        if (gSyncKnowledge.empty() && !folderSyncKnowledgeId.empty()) {
          gSyncKnowledge = folderSyncKnowledgeId;
        }
        ImGui::TextUnformatted("Folder");
        ImGui::SameLine(140.0f);
        if (gSyncFolder.empty()) {
          widgets::dimText("none chosen");
        } else {
          theme::pushMono();
          ImGui::TextUnformatted(gSyncFolder.c_str());
          theme::pop();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Choose...##syncfolder")) {
          const auto picked = native::chooseFolder();
          if (!picked.empty()) gSyncFolder = picked.front();
        }
        ImGui::TextUnformatted("Knowledge base");
        ImGui::SameLine(140.0f);
        knowledgeCombo("##synckb", knowledgeList, &gSyncKnowledge, 320.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload##synckb")) {
          gKnowledgeRequested = false;
          knowledgeList.clear();
        }
        ImGui::Dummy(ImVec2(0, 4));

        const bool syncing = folderSyncToken >= 0;
        if (syncing) {
          if (widgets::dangerButton("Stop sync", ImVec2(140, 0))) stopFolderSync();
          ImGui::SameLine();
          widgets::pill("live", palette.success);
        } else {
          const bool ready = !gSyncFolder.empty() && !gSyncKnowledge.empty();
          widgets::beginDisabled(!ready);
          if (widgets::primaryButton("Start sync", ImVec2(140, 0))) {
            syncFolderToKnowledge(gSyncFolder, gSyncKnowledge);
          }
          widgets::endDisabled(!ready);
        }
        if (!folderSyncStatus.empty()) widgets::dimText("%s", folderSyncStatus.c_str());
      }, width);
      break;
    }

    case Tab::UrlCapture: {
      card("capture", [&] {
        ImGui::TextUnformatted("Save a web page into a knowledge base");
        widgets::dimText("The page is fetched, stripped to text and uploaded as Markdown.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("URL");
        ImGui::SameLine(140.0f);
        ImGui::SetNextItemWidth(std::min(520.0f, width - 180.0f));
        ImGui::InputTextWithHint("##captureurl", "https://...", &gCaptureUrl);
        ImGui::TextUnformatted("Knowledge base");
        ImGui::SameLine(140.0f);
        knowledgeCombo("##capturekb", knowledgeList, &gCaptureKnowledge, 320.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload##capturekb")) {
          gKnowledgeRequested = false;
          knowledgeList.clear();
        }
        ImGui::Dummy(ImVec2(0, 4));
        const std::string url = core::trim(gCaptureUrl);
        const bool ready = (core::startsWith(url, "http://") || core::startsWith(url, "https://")) &&
                           !gCaptureKnowledge.empty();
        widgets::beginDisabled(!ready);
        if (widgets::primaryButton("Capture", ImVec2(140, 0))) {
          captureUrlToKnowledge(url, gCaptureKnowledge);
        }
        widgets::endDisabled(!ready);
        if (!folderSyncStatus.empty()) widgets::dimText("%s", folderSyncStatus.c_str());
      }, width);
      break;
    }
    case Tab::Mcp:
      drawMcpSection(width);
      break;
    case Tab::Terminal:
      drawTerminalSection(width);
      break;
    case Tab::Plugins:
      drawPluginsSection(width);
      break;
    case Tab::Git:
      drawGitSection(width);
      break;
    case Tab::Usage:
      drawUsageSection(width);
      break;
    case Tab::Backup:
      drawBackupSection(width);
      break;
    case Tab::Recipes:
      drawRecipesSection(width);
      break;
    case Tab::TestLoop:
      drawTestLoopSection(width);
      break;
    case Tab::Audit:
      drawAuditSection(width);
      break;
    case Tab::Traffic:
      drawTrafficSection(width);
      break;
    case Tab::Health:
      drawHealthSection(width);
      break;
    case Tab::Models:
      drawModelLibrarySection(width);
      break;
  }
  if (gTab != Tab::Health) healthTabHidden();

  paneEnd();
}

}  // namespace app
