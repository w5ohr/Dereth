// Test loop (Tools > Test loop): run the project's tests; on failure, ask the
// model for fixes with the failure digest and the files it names; apply the
// named file blocks (with backups and the apply journal, so one Undo puts
// everything back); run again - until green or the round budget is spent.
// Auto-apply is a switch; without it each round stops at the apply dialog.
#include "app.h"

#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "core/fileblocks.h"
#include "core/testloop.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Round {
  int number = 0;
  int exitCode = -1;
  bool green = false;
  std::string output;
  std::string reply;             // the model's fix reply
  std::vector<std::string> applied;
  double seconds = 0.0;
};

struct LoopState {
  std::vector<testloop::Runner> runners;
  int runner = 0;
  bool detected = false;
  std::string detectedFor;
  bool hasPytest = false;
  bool running = false;          // tests in flight
  bool asking = false;           // model in flight
  bool applying = false;
  bool active = false;           // the loop is going
  bool autoApply = true;
  int budget = 3;
  std::vector<Round> rounds;
  std::string status;
  std::string extraInstruction;
};
LoopState g;

constexpr int kTestTimeoutSeconds = 300;
constexpr size_t kFileCap = 40000;

}  // namespace

void App::testLoopDetect() {
  const std::string folder = gitRepo();
  if (folder.empty()) return;
  g.detectedFor = folder;
  g.detected = true;
  g.runners.clear();
  async::run([this, folder] {
    native::ProcessResult probe = native::runProcess({"python3", "-m", "pytest", "--version"}, folder, 20, false);
    const bool hasPytest = probe.exitCode == 0;
    std::vector<testloop::Runner> runners = testloop::detect(folder, hasPytest);
    async::post([this, runners, hasPytest] {
      g.runners = runners;
      g.hasPytest = hasPytest;
      g.runner = 0;
    });
  });
}

void App::testLoopRun(bool startLoop) {
  if (g.running || g.runners.empty()) return;
  const std::string folder = gitRepo();
  const testloop::Runner runner = g.runners[static_cast<size_t>(std::clamp(g.runner, 0, static_cast<int>(g.runners.size()) - 1))];
  if (startLoop) {
    g.active = true;
    g.rounds.clear();
  }
  g.running = true;
  g.status = "Running " + runner.name + "...";
  const int number = static_cast<int>(g.rounds.size()) + 1;
  async::run([this, folder, runner, number] {
    const double started = core::nowMillis();
    native::ProcessResult res = native::runProcess(runner.argv, folder, kTestTimeoutSeconds, false);
    const double seconds = (core::nowMillis() - started) / 1000.0;
    async::post([this, res, runner, number, seconds] {
      g.running = false;
      Round round;
      round.number = number;
      round.exitCode = res.timedOut ? -2 : res.exitCode;
      round.output = res.output;
      round.green = !res.timedOut && testloop::looksGreen(runner.name, res.exitCode, res.output);
      round.seconds = seconds;
      g.rounds.push_back(round);
      if (round.green) {
        g.status = "Green after " + std::to_string(number) + (number == 1 ? " run" : " rounds");
        if (g.active && number > 1) widgets::toast(widgets::ToastLevel::Success, "Tests pass after " + std::to_string(number - 1) + " fix round(s)");
        g.active = false;
        return;
      }
      g.status = res.timedOut ? "Timed out" : "Failing (exit " + std::to_string(res.exitCode) + ")";
      if (!g.active) return;
      if (number > g.budget) {
        g.status += " - budget of " + std::to_string(g.budget) + " rounds spent";
        g.active = false;
        widgets::toastInfo("Test loop stopped: still failing after " + std::to_string(g.budget) + " rounds");
        return;
      }
      testLoopAskForFix();
    });
  });
}

void App::testLoopAskForFix() {
  if (g.rounds.empty() || g.asking) return;
  const std::string folder = gitRepo();
  const Round& last = g.rounds.back();
  const std::vector<std::string> files = testloop::mentionedFiles(last.output, folder);
  std::string context;
  for (const std::string& rel : files) {
    bool ok = false;
    std::string body = core::readFile(folder + "/" + rel, &ok);
    if (!ok) continue;
    if (body.size() > kFileCap) body = body.substr(0, kFileCap) + "\n# [truncated]\n";
    context += "### " + rel + "\n```\n" + body + "```\n\n";
    if (context.size() > 120000) break;
  }
  const std::string runnerName = g.runners[static_cast<size_t>(g.runner)].name;
  std::string prompt = "The project's tests fail. Fix the code so they pass.\n\nRunner: " + runnerName +
                       "\n\nTest output:\n```\n" + testloop::digest(last.output) + "```\n\n";
  if (!context.empty()) prompt += "Files the output names, as they are now:\n\n" + context;
  if (!g.extraInstruction.empty()) prompt += "Also: " + g.extraInstruction + "\n\n";
  prompt += "Reply with the COMPLETE new contents of every file you change - every line of the file, not just the "
            "changed function - each in its own fenced block whose info string is `path=<relative path>` (for example "
            "```python path=src/app.py). Change as little as needed; do not weaken or delete tests unless the test "
            "itself is wrong, and say so. Keep prose to a line or two.";
  g.asking = true;
  g.status = "Asking " + (selectedModels.empty() ? std::string("the model") : selectedModels.front()) + " for a fix...";
  gitAsk("You are a careful engineer fixing failing tests in a codebase you can only see through the files shown. "
         "Return full files, not diffs.",
         prompt, [this](std::string reply) {
           g.asking = false;
           if (reply.empty()) {
             g.active = false;
             g.status = "No reply from the model";
             return;
           }
           g.rounds.back().reply = reply;
           const std::vector<fileblocks::Block> blocks = fileblocks::extract(reply);
           if (blocks.empty()) {
             g.active = false;
             g.status = "The reply named no files to change - loop stopped (see the reply below)";
             return;
           }
           if (g.autoApply) {
             testLoopApply(reply);
           } else {
             g.active = false;
             g.status = "Fix proposed - review it in the apply dialog, then Run again";
             applyFileBlocks(reply);
           }
         }, /*keepFences=*/true);
}

// Writes the reply's named files straight away (same backups and journal as
// the dialog path), then re-runs the tests.
void App::testLoopApply(const std::string& reply) {
  const std::string folder = gitRepo();
  g.applying = true;
  g.status = "Applying the fix...";
  async::run([this, folder, reply] {
    std::vector<std::string> written;
    std::string failed;
    for (const fileblocks::Block& block : fileblocks::extract(reply)) {
      std::string path = block.path;
      if (path.empty() || path[0] == '/' || path.find("..") != std::string::npos) continue;
      const std::string full = folder + "/" + path;
      bool ok = false;
      const std::string before = core::readFile(full, &ok);
      if (ok && before == block.code) continue;
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
      if ((ok && !core::writeFile(full + ".bak", before)) || !core::writeFile(full, block.code)) {
        failed = path;
        break;
      }
      written.push_back(path);
    }
    async::post([this, written, failed] {
      g.applying = false;
      if (!failed.empty()) {
        g.active = false;
        g.status = "Could not write " + failed;
        widgets::toastError(g.status);
        return;
      }
      g.rounds.back().applied = written;
      recordTestLoopApply(written);
      if (written.empty()) {
        g.active = false;
        g.status = "The fix changed nothing - loop stopped";
        return;
      }
      testLoopRun(false);
    });
  });
}

void App::testLoopStop() {
  g.active = false;
  g.status = "Stopped";
}

void App::drawTestLoopSection(float width) {
  const auto& palette = theme::palette();
  const std::string folder = gitRepo();
  widgets::sectionHeader("TEST LOOP");
  if (folder.empty()) {
    widgets::dimText("Pin a project folder (the folder icon in the composer) to run its tests.");
    return;
  }
  if (!g.detected || g.detectedFor != folder) testLoopDetect();
  widgets::dimText("Runs the tests; when they fail, asks the model for fixes with the failure output and the files it names,\n"
                   "applies the fix (backups kept, one Undo puts everything back), and runs again until green or the\n"
                   "round budget is spent. \"Undo the last apply to files\" in the palette reverts the latest round.");
  if (g.runners.empty()) {
    widgets::dimText("No test runner found in %s (pytest/unittest, npm test, ctest, swift test, go test, cargo test, make test).",
                     core::baseName(folder).c_str());
    if (ImGui::SmallButton("Look again")) testLoopDetect();
    return;
  }
  std::vector<const char*> names;
  for (const testloop::Runner& r : g.runners) names.push_back(r.name.c_str());
  ImGui::SetNextItemWidth(160);
  ImGui::Combo("##runner", &g.runner, names.data(), static_cast<int>(names.size()));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("rounds", &g.budget, 1, 8);
  ImGui::SameLine();
  ImGui::Checkbox("Auto-apply fixes", &g.autoApply);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: each round stops at the apply dialog so you review the fix first");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##extra", "extra instruction for the fixer (optional)", &g.extraInstruction);
  const bool busy = g.running || g.asking || g.applying;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
  widgets::beginDisabled(busy);
  if (widgets::primaryButton("Run tests", ImVec2(110, 0))) {
    g.active = false;
    g.rounds.clear();
    testLoopRun(false);
  }
  ImGui::SameLine();
  if (widgets::primaryButton("Run until green", ImVec2(150, 0))) testLoopRun(true);
  widgets::endDisabled(busy);
  ImGui::SameLine();
  if (g.active && ImGui::Button("Stop")) testLoopStop();
  ImGui::PopStyleVar();
  if (!g.status.empty()) {
    const bool green = !g.rounds.empty() && g.rounds.back().green;
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(green ? palette.success : busy ? palette.accent : palette.textDim));
    ImGui::TextUnformatted(g.status.c_str());
    ImGui::PopStyleColor();
  }

  // Rounds.
  for (auto it = g.rounds.rbegin(); it != g.rounds.rend(); ++it) {
    const Round& r = *it;
    ImGui::PushID(r.number);
    const std::string title = "Round " + std::to_string(r.number) + (r.green ? " - green" : r.exitCode == -2 ? " - timed out" : " - failing") +
                              " (" + std::to_string(static_cast<int>(r.seconds)) + " s)" +
                              (r.applied.empty() ? "" : " - applied " + std::to_string(r.applied.size()) + " file(s)");
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(r.green ? palette.success : palette.text));
    const bool open = ImGui::TreeNodeEx("##round", it == g.rounds.rbegin() ? ImGuiTreeNodeFlags_DefaultOpen : 0, "%s", title.c_str());
    ImGui::PopStyleColor();
    if (open) {
      if (!r.applied.empty()) {
        std::string files;
        for (const std::string& f : r.applied) files += (files.empty() ? "" : ", ") + f;
        widgets::dimText("Changed: %s", files.c_str());
      }
      ImGui::BeginChild("##out", ImVec2(width - 40.0f, 180.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
      theme::pushMono();
      ImGui::TextUnformatted(testloop::digest(r.output, 20000).c_str());
      theme::pop();
      ImGui::EndChild();
      if (!r.reply.empty() && ImGui::TreeNode("Model's reply")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 60.0f);
        ImGui::TextUnformatted(r.reply.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TreePop();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

}  // namespace app
