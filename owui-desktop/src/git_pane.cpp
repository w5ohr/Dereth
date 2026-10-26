// Git pane (Tools > Git): the project folder's working tree - status with
// stage/unstage toggles, the diff of what is staged (or of one file), a
// commit box whose message the model drafts from the staged diff, an
// "Explain this diff" that sends the diff to the chat, blame for a file or
// line range, and a PR description drafted from the branch's commits. All
// git calls run through native::runProcess on workers; nothing here needs
// the server except the two model calls.
#include "app.h"

#include <algorithm>
#include <sstream>

#include "core/async.h"
#include "core/common.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/diffview.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Entry {
  std::string path;
  char index = ' ';     // staged state
  char worktree = ' ';  // unstaged state
  bool untracked = false;
};

struct GitState {
  std::string repo;              // folder the last refresh ran in
  std::string branch, upstream;
  int ahead = 0, behind = 0;
  std::vector<Entry> entries;
  bool loading = false;
  bool isRepo = true;
  std::string error;
  // Diff view.
  std::string diffFor;           // "" = all staged, else a path
  bool diffStaged = true;
  std::string diffText;
  std::vector<diffview::FileDiff> diffs;
  bool diffLoading = false;
  // Commit.
  std::string message;
  bool drafting = false;
  bool committing = false;
  std::string lastCommit;
  // Blame.
  std::string blamePath;
  std::string blameRange;        // "10,20" or ""
  std::string blameText;
  bool blameLoading = false;
  // PR.
  std::string baseBranch = "main";
  std::string prText;
  bool prDrafting = false;
  double refreshedAt = 0.0;
};
GitState g;

native::ProcessResult git(const std::string& repo, std::vector<std::string> args, int timeout = 30) {
  args.insert(args.begin(), "git");
  args.insert(args.begin() + 1, "-c");
  args.insert(args.begin() + 2, "color.ui=never");
  return native::runProcess(args, repo, timeout, false);
}

std::string statusWord(char c) {
  switch (c) {
    case 'M': return "modified";
    case 'A': return "added";
    case 'D': return "deleted";
    case 'R': return "renamed";
    case 'C': return "copied";
    case 'U': return "conflict";
    case '?': return "untracked";
    default: return "";
  }
}

std::string clip(const std::string& s, size_t limit) {
  return s.size() > limit ? s.substr(0, limit) + "\n[... diff truncated at " + std::to_string(limit / 1000) + " kB ...]\n" : s;
}

}  // namespace

std::string App::gitRepo() const {
  if (!projectFolder.empty()) return projectFolder;
  const Json& extra = core::Settings::instance().extra;
  return extra.contains("projectFolder") && extra["projectFolder"].is_string() ? extra["projectFolder"].get<std::string>()
                                                                                : std::string();
}

void App::gitRefresh() {
  const std::string repo = gitRepo();
  if (repo.empty()) return;
  g.repo = repo;
  g.loading = true;
  async::run([this, repo] {
    native::ProcessResult status = git(repo, {"status", "--porcelain=v1", "--branch", "--untracked-files=all"});
    async::post([this, repo, status] {
      if (g.repo != repo) return;
      g.loading = false;
      g.refreshedAt = core::nowMillis();
      g.entries.clear();
      g.isRepo = status.exitCode == 0;
      g.error = g.isRepo ? "" : core::trim(status.output);
      if (!g.isRepo) return;
      for (const std::string& line : core::split(status.output, '\n')) {
        if (line.size() < 3) continue;
        if (core::startsWith(line, "## ")) {
          // "## main...origin/main [ahead 2, behind 1]"
          std::string rest = line.substr(3);
          g.ahead = g.behind = 0;
          const size_t bracket = rest.find(" [");
          if (bracket != std::string::npos) {
            const std::string flags = rest.substr(bracket + 2);
            size_t a = flags.find("ahead ");
            if (a != std::string::npos) g.ahead = std::atoi(flags.c_str() + a + 6);
            size_t b = flags.find("behind ");
            if (b != std::string::npos) g.behind = std::atoi(flags.c_str() + b + 7);
            rest = rest.substr(0, bracket);
          }
          const size_t dots = rest.find("...");
          g.branch = dots == std::string::npos ? rest : rest.substr(0, dots);
          g.upstream = dots == std::string::npos ? "" : rest.substr(dots + 3);
          continue;
        }
        Entry e;
        e.index = line[0];
        e.worktree = line[1];
        e.path = line.substr(3);
        const size_t arrow = e.path.find(" -> ");
        if (arrow != std::string::npos) e.path = e.path.substr(arrow + 4);
        e.untracked = e.index == '?';
        if (e.untracked) e.index = ' ';
        g.entries.push_back(e);
      }
      std::sort(g.entries.begin(), g.entries.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
      gitLoadDiff();
    });
  });
}

void App::gitLoadDiff() {
  const std::string repo = g.repo;
  const std::string path = g.diffFor;
  const bool staged = g.diffStaged;
  g.diffLoading = true;
  async::run([this, repo, path, staged] {
    std::vector<std::string> args = {"diff", "--no-ext-diff"};
    if (staged) args.push_back("--cached");
    if (!path.empty()) {
      args.push_back("--");
      args.push_back(path);
    }
    native::ProcessResult res = git(repo, args);
    std::string text = res.output;
    // An untracked file has no diff; show it as all additions.
    if (!staged && !path.empty() && core::trim(text).empty()) {
      bool ok = false;
      const std::string body = core::readFile(repo + "/" + path, &ok);
      if (ok && body.find('\0') == std::string::npos) text = diffview::generate(path, "", body);
    }
    std::vector<diffview::FileDiff> diffs = diffview::parse(text);
    async::post([this, repo, path, staged, text, diffs] {
      if (g.repo != repo || g.diffFor != path || g.diffStaged != staged) return;
      g.diffLoading = false;
      g.diffText = text;
      g.diffs = diffs;
    });
  });
}

void App::gitStage(const std::string& path, bool stage) {
  const std::string repo = g.repo;
  async::run([this, repo, path, stage] {
    native::ProcessResult res = stage ? git(repo, {"add", "--", path}) : git(repo, {"reset", "-q", "--", path});
    async::post([this, res] {
      if (res.exitCode != 0) widgets::toastError("git: " + core::trim(res.output));
      gitRefresh();
    });
  });
}

void App::gitStageAll(bool stage) {
  const std::string repo = g.repo;
  async::run([this, repo, stage] {
    native::ProcessResult res = stage ? git(repo, {"add", "-A"}) : git(repo, {"reset", "-q"});
    async::post([this, res] {
      if (res.exitCode != 0) widgets::toastError("git: " + core::trim(res.output));
      gitRefresh();
    });
  });
}

// One non-streaming model call with the chat's model (or the first selected).
void App::gitAsk(const std::string& system, const std::string& user, std::function<void(std::string)> done,
                 bool keepFences) {
  const std::string model = selectedModels.empty() ? (chat.models.empty() ? "" : chat.models.front())
                                                   : selectedModels.front();
  if (model.empty()) {
    widgets::toastError("Pick a model first");
    done("");
    return;
  }
  const Json payload{{"model", model},
                     {"stream", false},
                     {"messages", Json::array({Json{{"role", "system"}, {"content", system}},
                                               Json{{"role", "user"}, {"content", user}}})}};
  async::run([this, payload, done, keepFences] {
    api::Result res = api_.completion(payload);
    async::post([res, done, keepFences] {
      if (!res.ok) {
        widgets::toastError("Model call failed: " + api::errorMessage(res));
        done("");
        return;
      }
      std::string text;
      const Json& choices = res.data.value("choices", Json::array());
      if (!choices.empty()) text = choices[0].value("message", Json::object()).value("content", "");
      const size_t think = text.rfind("</think>");
      if (think != std::string::npos) text = text.substr(think + 8);
      text = core::trim(text);
      // Strip a fence the model may have wrapped the answer in.
      if (!keepFences && core::startsWith(text, "```")) {
        const size_t nl = text.find('\n');
        const size_t end = text.rfind("```");
        if (nl != std::string::npos && end != std::string::npos && end > nl) text = core::trim(text.substr(nl + 1, end - nl - 1));
      }
      done(text);
    });
  });
}

void App::gitDraftCommitMessage() {
  const std::string repo = g.repo;
  g.drafting = true;
  async::run([this, repo] {
    native::ProcessResult diff = git(repo, {"diff", "--cached", "--no-ext-diff", "--stat", "-p"});
    native::ProcessResult recent = git(repo, {"log", "--oneline", "-8"});
    const std::string staged = diff.output;
    const std::string log = recent.output;
    async::post([this, staged, log] {
      if (core::trim(staged).empty()) {
        g.drafting = false;
        widgets::toastInfo("Nothing is staged - stage files first");
        return;
      }
      gitAsk("You write git commit messages. Reply with the message only: a summary line of at most 72 characters in "
             "the imperative mood, then a blank line, then a short body (2-6 lines) saying what changed and why when "
             "the diff makes the why visible. No quotes, no fences, no 'This commit'. Match the style of the recent "
             "subjects when they follow a convention.",
             "Recent commits:\n" + log + "\nStaged diff:\n" + clip(staged, 60000),
             [this](std::string text) {
               g.drafting = false;
               if (!text.empty()) g.message = text;
             });
    });
  });
}

void App::gitCommit() {
  const std::string repo = g.repo;
  const std::string message = core::trim(g.message);
  if (message.empty()) {
    widgets::toastInfo("Write or draft a commit message first");
    return;
  }
  g.committing = true;
  async::run([this, repo, message] {
    native::ProcessResult res = git(repo, {"commit", "-q", "-m", message});
    native::ProcessResult head = git(repo, {"log", "--oneline", "-1"});
    async::post([this, res, head] {
      g.committing = false;
      if (res.exitCode != 0) {
        widgets::toastError("Commit failed: " + core::trim(res.output));
        return;
      }
      g.message.clear();
      g.lastCommit = core::trim(head.output);
      widgets::toast(widgets::ToastLevel::Success, "Committed " + g.lastCommit);
      gitRefresh();
    });
  });
}

void App::gitExplainDiff() {
  if (core::trim(g.diffText).empty()) {
    widgets::toastInfo("Nothing to explain - the diff is empty");
    return;
  }
  const std::string what = g.diffFor.empty() ? (g.diffStaged ? "the staged changes" : "the working tree changes")
                                             : g.diffFor;
  pane = Pane::Chat;
  sendMessage("Explain this git diff of " + what + " in " + core::baseName(g.repo) +
              ": what changes, why it might have been made, and anything risky or worth a second look.\n\n```diff\n" +
              clip(g.diffText, 60000) + "```");
}

void App::gitBlame() {
  const std::string repo = g.repo;
  const std::string path = core::trim(g.blamePath);
  const std::string range = core::trim(g.blameRange);
  if (path.empty()) return;
  g.blameLoading = true;
  async::run([this, repo, path, range] {
    std::vector<std::string> args = {"blame", "--date=short", "-w"};
    if (!range.empty()) {
      args.push_back("-L");
      args.push_back(range);
    }
    args.push_back("--");
    args.push_back(path);
    native::ProcessResult res = git(repo, args);
    async::post([this, res] {
      g.blameLoading = false;
      g.blameText = res.exitCode == 0 ? res.output : "git blame: " + core::trim(res.output);
    });
  });
}

void App::gitDraftPr() {
  const std::string repo = g.repo;
  const std::string base = core::trim(g.baseBranch);
  g.prDrafting = true;
  async::run([this, repo, base] {
    native::ProcessResult range = git(repo, {"log", "--reverse", "--format=%s%n%b", base + "..HEAD"});
    native::ProcessResult stat = git(repo, {"diff", "--stat", base + "...HEAD"});
    native::ProcessResult diff = git(repo, {"diff", "--no-ext-diff", base + "...HEAD"});
    const std::string commits = range.output, files = stat.output, patch = diff.output;
    const bool ok = range.exitCode == 0;
    async::post([this, ok, commits, files, patch, base] {
      if (!ok || core::trim(commits).empty()) {
        g.prDrafting = false;
        widgets::toastInfo("No commits on this branch beyond " + base);
        return;
      }
      gitAsk("You write pull request descriptions in Markdown. Reply with the description only: a one-line title as a "
             "level-1 heading, a 'Summary' section of 2-5 bullets on what changes and why, a 'Changes' section grouping "
             "the files touched, and a 'Testing' section saying what was verified or what a reviewer should try. "
             "Be concrete and short; no fences around the whole thing.",
             "Commits on the branch (oldest first):\n" + commits + "\nFiles changed:\n" + files + "\nDiff:\n" +
                 clip(patch, 60000),
             [this](std::string text) {
               g.prDrafting = false;
               if (!text.empty()) g.prText = text;
             });
    });
  });
}

void App::drawGitSection(float width) {
  const auto& palette = theme::palette();
  const std::string repo = gitRepo();
  widgets::sectionHeader("GIT");
  if (repo.empty()) {
    widgets::dimText("Pin a project folder (the folder icon in the composer) and its repository shows here.");
    return;
  }
  if (g.repo != repo || (g.refreshedAt == 0.0 && !g.loading)) gitRefresh();
  // Refresh when the window comes back or every 10 s while visible.
  if (!g.loading && core::nowMillis() - g.refreshedAt > 10000.0) gitRefresh();

  if (!g.isRepo) {
    widgets::dimText("%s is not a git repository.", core::baseName(repo).c_str());
    if (!g.error.empty()) widgets::dimText("%s", g.error.c_str());
    return;
  }

  // Header: branch, upstream, counts.
  theme::pushBold();
  ImGui::TextUnformatted(g.branch.c_str());
  theme::pop();
  ImGui::SameLine();
  std::string sync;
  if (!g.upstream.empty()) sync = g.upstream;
  if (g.ahead) sync += (sync.empty() ? "" : "  ") + std::to_string(g.ahead) + " ahead";
  if (g.behind) sync += (sync.empty() ? "" : "  ") + std::to_string(g.behind) + " behind";
  widgets::dimText("%s", sync.empty() ? "no upstream" : sync.c_str());
  ImGui::SameLine();
  if (widgets::ghostButton(g.loading ? "Refreshing..." : "Refresh")) gitRefresh();
  ImGui::SameLine();
  widgets::dimText("%s", repo.c_str());

  const float left = std::min(360.0f, width * 0.38f);
  ImGui::BeginChild("##gitleft", ImVec2(left, 0), ImGuiChildFlags_None);
  {
    int staged = 0, unstaged = 0;
    for (const Entry& e : g.entries) {
      if (e.index != ' ') ++staged;
      if (e.worktree != ' ' || e.untracked) ++unstaged;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
    if (ImGui::SmallButton("Stage all")) gitStageAll(true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Unstage all")) gitStageAll(false);
    ImGui::SameLine();
    if (ImGui::SmallButton(g.diffStaged && g.diffFor.empty() ? "Staged diff" : "Show staged diff")) {
      g.diffFor.clear();
      g.diffStaged = true;
      gitLoadDiff();
    }
    ImGui::PopStyleVar();
    widgets::dimText("%d staged, %d unstaged%s", staged, unstaged, g.entries.empty() ? " - clean" : "");

    ImGui::BeginChild("##gitfiles", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f), ImGuiChildFlags_Borders);
    for (const Entry& e : g.entries) {
      ImGui::PushID(e.path.c_str());
      bool isStaged = e.index != ' ';
      if (ImGui::Checkbox("##st", &isStaged)) gitStage(e.path, isStaged);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", isStaged ? "Staged - untick to unstage" : "Tick to stage");
      ImGui::SameLine();
      const char code = e.untracked ? '?' : (e.worktree != ' ' ? e.worktree : e.index);
      const ImU32 color = code == 'D' ? palette.danger : code == '?' || code == 'A' ? palette.success : palette.warning;
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(color));
      ImGui::Text("%c", code);
      ImGui::PopStyleColor();
      ImGui::SameLine();
      const bool showing = g.diffFor == e.path;
      if (ImGui::Selectable(e.path.c_str(), showing)) {
        g.diffFor = e.path;
        g.diffStaged = e.index != ' ' && e.worktree == ' ';
        gitLoadDiff();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s%s%s", statusWord(code).c_str(), e.index != ' ' && e.worktree != ' ' ? ", partly staged" : "",
                          "\nClick for its diff, right-click for blame");
      }
      if (ImGui::BeginPopupContextItem("##filemenu")) {
        if (ImGui::MenuItem("Blame this file")) {
          g.blamePath = e.path;
          g.blameRange.clear();
          gitBlame();
        }
        if (ImGui::MenuItem("Reveal in Finder")) platform::revealInFinder(g.repo + "/" + e.path);
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    if (g.entries.empty() && !g.loading) widgets::dimText("Working tree clean");
    ImGui::EndChild();

    // Commit box.
    ImGui::Dummy(ImVec2(0, 4));
    widgets::sectionHeader("COMMIT");
    ImGui::InputTextMultiline("##commitmsg", &g.message, ImVec2(-1.0f, 90.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
    widgets::beginDisabled(g.drafting || staged == 0);
    if (ImGui::Button(g.drafting ? "Drafting..." : "Draft message")) gitDraftCommitMessage();
    widgets::endDisabled(g.drafting || staged == 0);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("The model writes a message from the staged diff and the recent subjects; edit it before committing");
    }
    ImGui::SameLine();
    widgets::beginDisabled(g.committing || staged == 0 || core::trim(g.message).empty());
    if (widgets::primaryButton(g.committing ? "Committing..." : "Commit", ImVec2(110, 0))) gitCommit();
    widgets::endDisabled(g.committing || staged == 0 || core::trim(g.message).empty());
    ImGui::PopStyleVar();
    if (!g.lastCommit.empty()) widgets::dimText("Last: %s", g.lastCommit.c_str());

    // PR description.
    ImGui::Dummy(ImVec2(0, 4));
    widgets::sectionHeader("PULL REQUEST");
    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##base", "base branch", &g.baseBranch);
    ImGui::SameLine();
    widgets::beginDisabled(g.prDrafting);
    if (ImGui::SmallButton(g.prDrafting ? "Drafting..." : "Draft PR description")) gitDraftPr();
    widgets::endDisabled(g.prDrafting);
    if (!g.prText.empty()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Copy")) {
        platform::setClipboardText(g.prText);
        widgets::toast(widgets::ToastLevel::Success, "PR description copied");
      }
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // Right: diff / blame / PR text.
  ImGui::BeginChild("##gitright", ImVec2(0, 0), ImGuiChildFlags_None);
  {
    if (ImGui::BeginTabBar("##gitviews")) {
      if (ImGui::BeginTabItem("Diff")) {
        const std::string title = g.diffFor.empty() ? std::string("Staged changes") : g.diffFor + (g.diffStaged ? " (staged)" : " (working tree)");
        ImGui::TextUnformatted(title.c_str());
        ImGui::SameLine();
        if (!g.diffFor.empty()) {
          if (ImGui::SmallButton(g.diffStaged ? "Show working tree" : "Show staged")) {
            g.diffStaged = !g.diffStaged;
            gitLoadDiff();
          }
          ImGui::SameLine();
        }
        widgets::beginDisabled(core::trim(g.diffText).empty());
        if (ImGui::SmallButton("Explain in chat")) gitExplainDiff();
        widgets::endDisabled(core::trim(g.diffText).empty());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy diff")) platform::setClipboardText(g.diffText);
        ImGui::BeginChild("##gitdiff", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        if (g.diffLoading) {
          widgets::loadingRow("Reading diff...");
        } else if (g.diffs.empty()) {
          widgets::dimText("%s", g.diffFor.empty() ? "Nothing staged." : "No changes.");
        } else {
          diffview::render("##gitdiffview", g.diffs, false, ImGui::GetContentRegionAvail().x);
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Blame")) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##blamepath", "path in the repository", &g.blamePath);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputTextWithHint("##blamerange", "lines 10,20", &g.blameRange);
        ImGui::SameLine();
        if (ImGui::SmallButton(g.blameLoading ? "..." : "Blame")) gitBlame();
        ImGui::BeginChild("##blameout", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        theme::pushMono();
        ImGui::TextUnformatted(g.blameText.empty() ? "Pick a file (right-click one in the list, or type its path)." : g.blameText.c_str());
        theme::pop();
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("PR description")) {
        if (g.prText.empty()) {
          widgets::dimText("\"Draft PR description\" on the left writes one from the commits since the base branch.");
        } else {
          ImGui::InputTextMultiline("##prtext", &g.prText, ImVec2(-1.0f, -1.0f));
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();
}

}  // namespace app
