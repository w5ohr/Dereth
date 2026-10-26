// Code tools: run/lint code blocks, apply a block to a file, folder and git
// context, build-error explainer, command runner, project mode.
//
// Owned by work package C in docs/FEATURE-PLAN.md. Every process runs on a
// worker through native::runProcess; results come back with async::post, so
// the draw functions here only ever read state.
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unistd.h>

#include "app.h"
#include "core/playground.h"

#include "plugins/plugin_host.h"
#include "core/async.h"
#include "core/common.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "core/fileblocks.h"
#include "core/fileeditor.h"
#include "core/repomap.h"
#include "ui/diffview.h"
#include "ui/highlight.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;

namespace app {
namespace {

constexpr int kRunTimeoutSeconds = 30;
constexpr int kCommandTimeoutSeconds = 120;
constexpr uintmax_t kMaxFileBytes = 200 * 1024;
constexpr size_t kMaxFolderBytes = 1536 * 1024;
constexpr size_t kMaxProjectFileBytes = 600 * 1024;
constexpr size_t kMaxCommandOutput = 1024 * 1024;
const char* kJsc = "/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc";

// Directories that are never useful as context even without a .gitignore.
const std::set<std::string> kAlwaysSkip = {".git", "node_modules", "__pycache__", ".venv",
                                           "DerivedData", ".DS_Store", ".svn", ".hg"};

std::string codeKey(const std::string& code) {
  return std::to_string(std::hash<std::string>{}(code));
}

// ------------------------------------------------------------ tool lookup

// A GUI app launched from Finder gets PATH=/usr/bin:/bin:/usr/sbin:/sbin, so
// Homebrew, rustup and the Go installer locations are searched explicitly.
std::string findTool(const std::string& name) {
  static std::map<std::string, std::string> cache;
  auto hit = cache.find(name);
  if (hit != cache.end()) return hit->second;
  std::vector<std::string> dirs;
  if (const char* path = std::getenv("PATH")) dirs = core::split(path, ':');
  const std::string home = native::homeDirectory();
  for (const std::string& extra : {std::string("/opt/homebrew/bin"), std::string("/usr/local/bin"),
                                   home + "/.cargo/bin", std::string("/usr/local/go/bin"),
                                   std::string("/opt/homebrew/opt/go/bin"), std::string("/usr/bin"),
                                   std::string("/bin")}) {
    dirs.push_back(extra);
  }
  std::string found;
  for (const std::string& dir : dirs) {
    if (dir.empty()) continue;
    const std::string candidate = dir + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) {
      found = candidate;
      break;
    }
  }
  cache[name] = found;
  return found;
}

// ---------------------------------------------------------------- runners

// One block becomes a source file plus a sequence of argv steps run in a
// scratch directory; the first failing step ends the run.
struct Plan {
  std::string fileName;
  std::string source;
  std::vector<std::vector<std::string>> steps;
  std::map<std::string, std::string> extraFiles;
};

bool makePlan(const std::string& rawLanguage, const std::string& code, bool lint, Plan* plan,
              std::string* why) {
  const std::string lang = highlight::normalizeLanguage(rawLanguage);
  plan->source = code;
  auto need = [&](const std::string& tool) {
    std::string path = findTool(tool);
    if (path.empty() && why) *why = tool + " is not installed";
    return path;
  };
  if (lang == "python") {
    const std::string py = need("python3");
    if (py.empty()) return false;
    plan->fileName = "main.py";
    if (lint) plan->steps = {{py, "-I", "-m", "py_compile", "main.py"}};
    else plan->steps = {{py, "-I", "main.py"}};
  } else if (lang == "shell") {
    plan->fileName = "main.sh";
    if (lint) plan->steps = {{"/bin/bash", "-n", "main.sh"}};
    else plan->steps = {{"/bin/bash", "main.sh"}};
  } else if (lang == "javascript") {
    if (access(kJsc, X_OK) != 0) {
      if (why) *why = "JavaScriptCore's jsc is missing";
      return false;
    }
    plan->fileName = "main.js";
    if (lint) {
      plan->steps = {{kJsc, "-e", "checkSyntax(\"main.js\")"}};
    } else {
      // jsc has print() but no console; the shim lives in its own file so
      // error line numbers still match the block.
      plan->extraFiles["console-shim.js"] =
          "var console = { log: (...a) => print(...a), info: (...a) => print(...a),"
          " warn: (...a) => print(...a), error: (...a) => print(...a) };\n";
      plan->steps = {{kJsc, "console-shim.js", "main.js"}};
    }
  } else if (lang == "cpp" || lang == "c") {
    const bool cpp = lang == "cpp";
    const std::string cc = need(cpp ? "clang++" : "clang");
    if (cc.empty()) return false;
    plan->fileName = cpp ? "main.cpp" : "main.c";
    const std::string standard = cpp ? "-std=c++17" : "-std=c11";
    if (lint) {
      plan->steps = {{cc, standard, "-fsyntax-only", "-Wall", "-Wextra", plan->fileName}};
    } else {
      plan->steps = {{cc, standard, "-O1", "-o", "out", plan->fileName}, {"./out"}};
    }
  } else if (lang == "go") {
    const std::string go = need("go");
    if (go.empty()) return false;
    plan->fileName = "main.go";
    if (lint) {
      const std::string gofmt = need("gofmt");
      if (gofmt.empty()) return false;
      plan->steps = {{gofmt, "-e", "-l", "main.go"}};
    } else {
      // The sandbox only allows writes in the scratch dir, so Go's build
      // cache has to live there too.
      plan->steps = {{"/usr/bin/env", "GOCACHE={dir}/.gocache", "GOPATH={dir}/.gopath",
                      "GOFLAGS=-mod=mod", go, "run", "main.go"}};
    }
  } else if (lang == "rust") {
    const std::string rustc = need("rustc");
    if (rustc.empty()) return false;
    plan->fileName = "main.rs";
    if (lint) plan->steps = {{rustc, "--edition", "2021", "--emit=metadata", "-o", "main.rmeta", "main.rs"}};
    else plan->steps = {{rustc, "--edition", "2021", "-o", "out", "main.rs"}, {"./out"}};
  } else if (lang == "ruby") {
    const std::string ruby = need("ruby");
    if (ruby.empty()) return false;
    plan->fileName = "main.rb";
    if (lint) plan->steps = {{ruby, "-c", "main.rb"}};
    else plan->steps = {{ruby, "main.rb"}};
  } else {
    if (why) *why = "No local runner for " + (rawLanguage.empty() ? std::string("untagged blocks") : rawLanguage);
    return false;
  }
  return true;
}

bool canRun(const std::string& language, bool lint) {
  static std::map<std::string, bool> cache;
  const std::string key = (lint ? "lint:" : "run:") + language;
  auto hit = cache.find(key);
  if (hit != cache.end()) return hit->second;
  Plan plan;
  return cache[key] = makePlan(language, "", lint, &plan, nullptr);
}

// Writes the plan into a fresh scratch directory and runs it sandboxed:
// no network, writes only inside that directory and the temp folders.
CodeResult executePlan(const Plan& plan, bool lint) {
  CodeResult result;
  std::error_code ec;
  const fs::path base = fs::temp_directory_path(ec) / ("owui-run-" + core::uuid());
  fs::create_directories(base, ec);
  if (ec) {
    result.exitCode = -1;
    result.output = "Could not create a scratch directory: " + ec.message();
    return result;
  }
  // sandbox-exec matches subpaths literally, so /var must be /private/var.
  const std::string dir = fs::canonical(base, ec).string();
  core::writeFile(dir + "/" + plan.fileName, plan.source);
  for (const auto& [name, text] : plan.extraFiles) core::writeFile(dir + "/" + name, text);

  std::string output;
  for (std::vector<std::string> argv : plan.steps) {
    for (std::string& arg : argv) arg = core::replaceAll(arg, "{dir}", dir);
    native::ProcessResult step = native::runProcess(argv, dir, kRunTimeoutSeconds, true);
    output += step.output;
    result.exitCode = step.exitCode;
    result.timedOut = step.timedOut;
    if (step.timedOut || step.exitCode != 0) break;
  }
  // Paths into the scratch dir are noise; show bare file names.
  output = core::replaceAll(output, dir + "/", "");
  if (core::startsWith(dir, "/private")) output = core::replaceAll(output, dir.substr(8) + "/", "");
  if (lint && result.exitCode == 0 && !result.timedOut && core::trim(output).empty()) {
    output = "No problems found.";
  }
  result.output = output;
  fs::remove_all(dir, ec);
  return result;
}

// ------------------------------------------------------- folder walking

// .gitignore support: `name`, `dir/`, `*.ext`, leading `/`, `**`, `?` and
// `[...]`. Negations are ignored (the rule is simply dropped).
struct IgnoreRule {
  std::string base;      // absolute directory holding the .gitignore
  std::string pattern;
  bool dirOnly = false;
  bool anchored = false; // matched against the path from `base`, not the name
};

bool globMatch(const char* p, const char* s) {
  while (*p) {
    if (p[0] == '*' && p[1] == '*') {
      p += 2;
      if (*p == '/') {
        ++p;
        // `**/` matches zero or more whole directories.
        if (globMatch(p, s)) return true;
        for (const char* c = s; *c; ++c) {
          if (*c == '/' && globMatch(p, c + 1)) return true;
        }
        return false;
      }
      if (!*p) return true;
      for (const char* c = s;; ++c) {
        if (globMatch(p, c)) return true;
        if (!*c) return false;
      }
    }
    if (*p == '*') {
      ++p;
      for (const char* c = s;; ++c) {
        if (globMatch(p, c)) return true;
        if (!*c || *c == '/') return false;
      }
    }
    if (!*s) return false;
    if (*p == '?') {
      if (*s == '/') return false;
    } else if (*p == '[') {
      const char* q = p + 1;
      bool negate = *q == '!' || *q == '^';
      if (negate) ++q;
      bool matched = false;
      while (*q && *q != ']') {
        if (q[1] == '-' && q[2] && q[2] != ']') {
          if (*s >= q[0] && *s <= q[2]) matched = true;
          q += 3;
        } else {
          if (*s == *q) matched = true;
          ++q;
        }
      }
      if (!*q || matched == negate) return false;
      p = q;
    } else {
      if (*p == '\\' && p[1]) ++p;
      if (*p != *s) return false;
    }
    ++p;
    ++s;
  }
  return !*s;
}

void loadGitignore(const std::string& dir, std::vector<IgnoreRule>* rules) {
  bool ok = false;
  const std::string text = core::readFile(dir + "/.gitignore", &ok);
  if (!ok) return;
  for (std::string line : core::split(text, '\n')) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Trailing spaces are insignificant unless escaped; we just trim them.
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (line.empty() || line[0] == '#' || line[0] == '!') continue;
    IgnoreRule rule;
    rule.base = dir;
    if (line.back() == '/') {
      rule.dirOnly = true;
      line.pop_back();
    }
    if (line[0] == '/') {
      rule.anchored = true;
      line.erase(0, 1);
    }
    if (line.find('/') != std::string::npos) rule.anchored = true;
    if (line.empty()) continue;
    rule.pattern = line;
    rules->push_back(rule);
  }
}

bool ignored(const std::vector<IgnoreRule>& rules, const std::string& absPath, bool isDir) {
  for (const IgnoreRule& rule : rules) {
    if (rule.dirOnly && !isDir) continue;
    if (!core::startsWith(absPath, rule.base + "/")) continue;
    const std::string rel = absPath.substr(rule.base.size() + 1);
    const std::string subject = rule.anchored ? rel : core::baseName(rel);
    if (globMatch(rule.pattern.c_str(), subject.c_str())) return true;
  }
  return false;
}

struct WalkEntry {
  std::string rel;   // relative to the walk root, '/'-separated
  bool dir = false;
  uintmax_t size = 0;
};

// Rules from the .gitignore files between the repository root and `root`, so
// attaching a subfolder still honours the repo's top-level ignores.
std::vector<IgnoreRule> ancestorRules(const fs::path& root) {
  std::error_code ec;
  if (fs::exists(root / ".git", ec)) return {};   // root is the repository itself
  std::vector<fs::path> chain;
  for (fs::path dir = root.parent_path(); !dir.empty(); dir = dir.parent_path()) {
    chain.push_back(dir);
    std::error_code ec;
    if (fs::exists(dir / ".git", ec) || dir == dir.parent_path()) break;
  }
  // Only use the chain when it ends in a repository.
  if (chain.empty() || !fs::exists(chain.back() / ".git", ec)) return {};
  std::vector<IgnoreRule> rules;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) loadGitignore(it->string(), &rules);
  return rules;
}

void walkDir(const fs::path& dir, const std::string& relPrefix, int depth, int maxDepth,
             size_t maxEntries, std::vector<IgnoreRule> rules, std::vector<WalkEntry>* out) {
  loadGitignore(dir.string(), &rules);
  std::vector<fs::directory_entry> children;
  std::error_code ec;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    children.push_back(*it);
  }
  // Directories first, then files, each alphabetically, like an editor tree.
  std::sort(children.begin(), children.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
    std::error_code e;
    const bool ad = a.is_directory(e), bd = b.is_directory(e);
    if (ad != bd) return ad;
    return a.path().filename().string() < b.path().filename().string();
  });
  for (const fs::directory_entry& child : children) {
    if (out->size() >= maxEntries) return;
    const std::string name = child.path().filename().string();
    if (kAlwaysSkip.count(name)) continue;
    std::error_code e;
    if (child.is_symlink(e)) continue;   // avoids loops and escaping the folder
    const bool isDir = child.is_directory(e);
    if (!isDir && !child.is_regular_file(e)) continue;
    if (ignored(rules, child.path().string(), isDir)) continue;
    WalkEntry entry;
    entry.rel = relPrefix + name;
    entry.dir = isDir;
    entry.size = isDir ? 0 : child.file_size(e);
    out->push_back(entry);
    if (isDir && depth + 1 < maxDepth) {
      walkDir(child.path(), entry.rel + "/", depth + 1, maxDepth, maxEntries, rules, out);
    }
  }
}

std::vector<WalkEntry> walkFolder(const std::string& folder, int maxDepth, size_t maxEntries) {
  std::vector<WalkEntry> out;
  std::error_code ec;
  const fs::path root = fs::canonical(folder, ec);
  if (ec) return out;
  walkDir(root, "", 0, maxDepth, maxEntries, ancestorRules(root), &out);
  return out;
}

bool looksBinary(const std::string& bytes) {
  return bytes.find('\0', 0) < std::min<size_t>(bytes.size(), 1024);
}

std::string fenceLanguage(const std::string& path) {
  const std::string ext = core::lower(core::extension(path));
  const std::string normalized = highlight::normalizeLanguage(ext);
  return normalized.empty() ? ext : normalized;
}

// Picks a fence longer than any backtick run in the text so files that
// contain Markdown don't break out of their block.
std::string fenceFor(const std::string& text) {
  size_t longest = 0, run = 0;
  for (char c : text) {
    run = c == '`' ? run + 1 : 0;
    longest = std::max(longest, run);
  }
  return std::string(std::max<size_t>(3, longest + 1), '`');
}

std::string fenced(const std::string& language, const std::string& text) {
  const std::string fence = fenceFor(text);
  std::string out = fence + language + "\n" + text;
  if (!text.empty() && text.back() != '\n') out += "\n";
  return out + fence + "\n";
}

std::string fencedFile(const std::string& title, const std::string& path, const std::string& text) {
  return "### " + title + "\n" + fenced(fenceLanguage(path), text) + "\n";
}

// ------------------------------------------------------ apply-to-file modal

// One target of the apply dialog. `diffs[i]` in ApplyState pairs with
// `files[i]`; they are kept apart because diffview::render takes the vector.
struct ApplyFile {
  std::string path;     // absolute
  std::string before;
  bool created = false; // the diff adds a file that does not exist yet
  std::string error;    // why the dry run failed, shown in the dialog
};

// UI-thread-only state for the single apply dialog. Workers fill it via post.
struct ApplyState {
  bool show = false;
  bool loading = false;
  bool writing = false;
  int generation = 0;   // ignores a stale load after the dialog was reopened
  std::vector<ApplyFile> files;
  std::vector<diffview::FileDiff> diffs;
};
ApplyState gApply;

// The last apply, for "Undo last apply": every file written, with its .bak
// (or none when the file was created). Also journalled to support/apply-history.json.
struct AppliedFile {
  std::string path;
  bool created = false;
};
std::vector<AppliedFile> gLastApply;

std::string applyHistoryPath() { return core::supportDir() + "/apply-history.json"; }

void journalApply(const std::vector<AppliedFile>& files) {
  bool ok = false;
  const std::string raw = core::readFile(applyHistoryPath(), &ok);
  Json history = ok ? Json::parse(raw, nullptr, false) : Json::array();
  if (history.is_discarded() || !history.is_array()) history = Json::array();
  Json entry{{"at", static_cast<int64_t>(core::nowSeconds())}, {"files", Json::array()}};
  for (const AppliedFile& f : files) entry["files"].push_back(Json{{"path", f.path}, {"created", f.created}});
  history.push_back(entry);
  while (history.size() > 50) history.erase(history.begin());
  core::writeFile(applyHistoryPath(), history.dump(2));
}

// Puts the .bak copies back (or removes created files). Used for "Undo last
// apply" and when a write in the middle of an apply fails.
std::string rollback(const std::vector<AppliedFile>& files) {
  std::string failed;
  for (auto it = files.rbegin(); it != files.rend(); ++it) {
    std::error_code ec;
    if (it->created) {
      fs::remove(it->path, ec);
      continue;
    }
    bool ok = false;
    const std::string before = core::readFile(it->path + ".bak", &ok);
    if (!ok || !core::writeFile(it->path, before)) failed = it->path;
    else fs::remove(it->path + ".bak", ec);
  }
  return failed;
}

// diffview::parse reads the empty piece after a final newline as a blank
// context line, which cannot be placed in an empty (new) file and can pull a
// hunk towards the wrong blank line elsewhere. Trailing newlines carry no
// diff content, so drop them first.
std::vector<diffview::FileDiff> parseDiff(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return diffview::parse(text);
}

int acceptedHunks(const diffview::FileDiff& diff) {
  return static_cast<int>(std::count_if(diff.hunks.begin(), diff.hunks.end(),
                                        [](const diffview::Hunk& h) { return h.accepted; }));
}

// Reads every target and dry-runs its patch on a worker, then opens the dialog.
void loadApply(std::vector<std::string> paths, std::vector<bool> created,
               std::vector<diffview::FileDiff> diffs) {
  gApply = ApplyState{gApply.show, true, false, gApply.generation + 1, {}, {}};
  gApply.show = true;
  const int generation = gApply.generation;
  async::run([paths, created, diffs, generation] {
    std::vector<ApplyFile> files;
    for (size_t i = 0; i < paths.size(); ++i) {
      ApplyFile file;
      file.path = paths[i];
      file.created = created[i];
      bool ok = false;
      file.before = core::readFile(paths[i], &ok);
      if (!ok && !file.created) {
        file.error = "Could not read the file";
      } else {
        std::string result;
        if (!diffview::apply(diffs[i], file.before, &result, &file.error) && file.error.empty()) {
          file.error = "The patch does not apply";
        }
      }
      files.push_back(file);
    }
    async::post([files, diffs, generation] {
      if (!gApply.show || gApply.generation != generation) return;
      gApply.loading = false;
      gApply.files = files;
      gApply.diffs = diffs;
    });
  });
}

void confirmApply() {
  // All targets are computed before anything is written, so a hunk that
  // no longer applies leaves every file untouched.
  std::vector<std::pair<ApplyFile, std::string>> writes;
  std::string failure;
  for (size_t i = 0; i < gApply.files.size() && failure.empty(); ++i) {
    if (acceptedHunks(gApply.diffs[i]) == 0) continue;
    std::string result, error;
    if (diffview::apply(gApply.diffs[i], gApply.files[i].before, &result, &error)) {
      writes.push_back({gApply.files[i], result});
    } else {
      failure = core::baseName(gApply.files[i].path) + ": " + (error.empty() ? "does not apply" : error);
    }
  }
  if (!failure.empty()) {
    widgets::toastError("Could not apply " + failure);
  } else {
    gApply.writing = true;
    const std::string editorHistory = core::supportDir() + "/editor-history";
    async::run([writes, editorHistory] {
      std::string failed;
      std::vector<AppliedFile> done;
      for (const auto& [file, result] : writes) {
        std::error_code ec;
        if (file.created) fs::create_directories(fs::path(file.path).parent_path(), ec);
        // The .bak keeps the pre-apply text one undo away.
        const bool ok = (file.created || core::writeFile(file.path + ".bak", file.before)) &&
                        core::writeFile(file.path, result);
        if (!ok) {
          failed = file.path;
          break;
        }
        done.push_back({file.path, file.created});
        // The editor's history: the file before and after this apply.
        if (!file.created) fileeditor::record(editorHistory, file.path, file.before, "before apply");
        fileeditor::record(editorHistory, file.path, result, "apply");
      }
      if (!failed.empty()) {
        // One transaction: a write that fails puts the earlier files back.
        rollback(done);
        done.clear();
      } else if (!done.empty()) {
        journalApply(done);
      }
      const size_t count = writes.size();
      const std::string first = writes.empty() ? "" : core::baseName(writes.front().first.path);
      async::post([failed, count, first, done] {
        gApply.writing = false;
        if (!failed.empty()) {
          widgets::toastError("Could not write " + failed + " - the other files were put back");
          return;
        }
        gLastApply = done;
        gApply.show = false;
        gApply.diffs.clear();
        for (const AppliedFile& f : done) App::instance().refreshEditorHistory(f.path);
        widgets::toast(widgets::ToastLevel::Success,
                       count == 1 ? "Wrote " + first + " (backup beside it as .bak)"
                                  : "Wrote " + std::to_string(count) + " files (backups beside them as .bak)");
      });
    });
  }
}

void drawApplyModal() {
  if (!gApply.show) return;
  ImGui::OpenPopup("Apply to file");
  ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_Appearing);
  bool open = true;
  if (ImGui::BeginPopupModal("Apply to file", &open)) {
    const auto& palette = theme::palette();
    int total = 0, accepted = 0;
    for (const auto& diff : gApply.diffs) {
      total += static_cast<int>(diff.hunks.size());
      accepted += acceptedHunks(diff);
    }
    if (gApply.files.size() > 1) {
      widgets::dimText("%zu files, applied together - if one cannot be written, none are changed.", gApply.files.size());
    }
    for (const ApplyFile& file : gApply.files) {
      ImGui::PushID(file.path.c_str());
      if (ImGui::SmallButton("Edit")) {   // the file as it is now, in the editor; the dialog closes
        App::instance().openFileEditor(file.path);
        gApply.show = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
      ImGui::SameLine();
      ImGui::TextUnformatted((file.path + (file.created ? "  (new file)" : "")).c_str());
      if (!file.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
        ImGui::TextWrapped("  %s", file.error.c_str());
        ImGui::PopStyleColor();
      }
    }
    ImGui::Separator();
    ImGui::BeginChild("##applydiff", ImVec2(0, -40), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (gApply.loading) {
      widgets::loadingRow("Reading file...");
    } else {
      diffview::render("##apply", gApply.diffs, true, ImGui::GetContentRegionAvail().x);
    }
    ImGui::EndChild();
    const bool disabled = gApply.loading || gApply.writing || accepted == 0;
    widgets::beginDisabled(disabled);
    const std::string label = "Apply " + std::to_string(accepted) + " of " + std::to_string(total) +
                              (total == 1 ? " change" : " changes");
    if (widgets::primaryButton(label.c_str())) confirmApply();
    widgets::endDisabled(disabled);
    ImGui::SameLine();
    if (widgets::ghostButton("Cancel")) open = false;
    ImGui::EndPopup();
  }
  if (!open) {
    gApply.show = false;
    gApply.diffs.clear();
    gApply.files.clear();
  }
}

// ---------------------------------------------------------- project mode

std::string gTreeFolder;      // folder `projectTree` was listed from
std::vector<repomap::FileMap> gRepoMap;   // built for gRepoMapFolder
std::string gRepoMapFolder;
bool gRepoMapBuilding = false;
constexpr size_t kRepoMapChars = 32000;   // ~8k tokens at most, in the compact form
bool gTreeLoading = false;
bool gProjectLoaded = false;
std::string gProjectFilter;

std::string currentProjectFolder(const App& app) {
  if (!app.projectFolder.empty()) return app.projectFolder;
  const Json& extra = core::Settings::instance().extra;
  return extra.contains("projectFolder") && extra["projectFolder"].is_string()
             ? extra["projectFolder"].get<std::string>()
             : std::string();
}

std::string resultBoxTitle(const CodeResult& r, bool lint) {
  if (r.running) return lint ? "Linting..." : "Running...";
  if (r.timedOut) return "Timed out after " + std::to_string(kRunTimeoutSeconds) + " s";
  return std::string(lint ? "Lint" : "Output") + " - exit " + std::to_string(r.exitCode);
}

}  // namespace

// ================================================================== run

void App::runCodeBlock(const std::string& code, const std::string& language) {
  Plan plan;
  std::string why;
  if (!makePlan(language, code, false, &plan, &why)) {
    widgets::toastInfo(why);
    return;
  }
  const std::string key = codeKey(code);
  if (codeResults[key].running) return;
  codeResults[key] = CodeResult{};
  codeResults[key].running = true;
  async::run([this, plan, key] {
    CodeResult result = executePlan(plan, false);
    async::post([this, key, result] { codeResults[key] = result; });
  });
}

void App::lintCodeBlock(const std::string& code, const std::string& language) {
  Plan plan;
  std::string why;
  if (!makePlan(language, code, true, &plan, &why)) {
    widgets::toastInfo(why);
    return;
  }
  const std::string key = "lint:" + codeKey(code);
  if (codeResults[key].running) return;
  codeResults[key] = CodeResult{};
  codeResults[key].running = true;
  async::run([this, plan, key] {
    CodeResult result = executePlan(plan, true);
    async::post([this, key, result] { codeResults[key] = result; });
  });
}

// ======================================================== apply to file

void App::applyCodeBlockToFile(const std::string& code) {
  if (diffview::looksLikeDiff(code)) {
    // A diff names its own targets: resolve them against the project folder
    // and only ask for a file when a path cannot be found.
    std::vector<diffview::FileDiff> diffs;
    int deletions = 0;
    for (diffview::FileDiff& diff : parseDiff(code)) {
      if (diff.hunks.empty()) continue;
      if (diff.newPath == "/dev/null") {
        ++deletions;
        continue;
      }
      diffs.push_back(diff);
    }
    if (deletions > 0) widgets::toastInfo("File deletions in the diff were left out");
    if (diffs.empty()) {
      widgets::toastInfo("The diff has no changes to apply");
      return;
    }
    const std::string folder = currentProjectFolder(*this);
    std::vector<std::string> paths;
    std::vector<bool> created;
    for (const diffview::FileDiff& diff : diffs) {
      const bool isNew = diff.oldPath == "/dev/null";
      const std::string rel = !diff.newPath.empty() ? diff.newPath : diff.oldPath;
      std::string path;
      std::error_code ec;
      if (!rel.empty() && rel[0] == '/' && fs::is_regular_file(rel, ec)) {
        path = rel;
      } else if (!folder.empty() && !rel.empty() && (isNew || fs::is_regular_file(folder + "/" + rel, ec))) {
        path = folder + "/" + rel;
      } else {
        widgets::toastInfo("Choose the file for " + (rel.empty() ? std::string("this diff") : rel));
        const std::vector<std::string> picked = platform::openFiles(false);
        if (picked.empty()) return;
        path = picked.front();
      }
      paths.push_back(path);
      created.push_back(isNew && !fs::exists(path, ec));
    }
    loadApply(paths, created, diffs);
    return;
  }

  const std::vector<std::string> picked = platform::openFiles(false);
  if (picked.empty()) return;
  const std::string path = picked.front();
  // Anything that is not a diff replaces the whole file; the dialog shows
  // what that would change, hunk by hunk.
  async::run([path, code] {
    bool ok = false;
    const std::string before = core::readFile(path, &ok);
    std::vector<diffview::FileDiff> diffs;
    if (ok) diffs = parseDiff(diffview::generate(core::baseName(path), before, code));
    async::post([path, ok, diffs] {
      if (!ok) {
        widgets::toastError("Could not read " + path);
      } else if (diffs.empty() || diffs.front().hunks.empty()) {
        widgets::toastInfo("No changes - the block matches " + core::baseName(path));
      } else {
        loadApply({path}, {false}, {diffs.front()});
      }
    });
  });
}

// ================================================= multi-file apply, undo

// The file blocks a reply names, resolved against the project folder.
int App::countFileBlocks(const std::string& content) const {
  return static_cast<int>(fileblocks::extract(content).size());
}

void App::applyFileBlocks(const std::string& content) {
  const std::vector<fileblocks::Block> blocks = fileblocks::extract(content);
  if (blocks.empty()) {
    widgets::toastInfo("No code block in this reply names a file");
    return;
  }
  std::string folder = currentProjectFolder(*this);
  if (folder.empty()) {
    widgets::toastInfo("Choose the folder the files belong to");
    const std::vector<std::string> picked = native::chooseFolder();
    if (picked.empty()) return;
    folder = picked.front();
  }
  std::vector<std::string> paths;
  std::vector<bool> created;
  std::vector<diffview::FileDiff> diffs;
  async::run([this, blocks, folder] {
    std::vector<std::string> paths;
    std::vector<bool> created;
    std::vector<diffview::FileDiff> diffs;
    int unchanged = 0;
    for (const fileblocks::Block& block : blocks) {
      std::error_code ec;
      std::string path = block.path;
      if (path[0] == '~') path = native::homeDirectory() + path.substr(1);
      if (path[0] != '/') path = folder + "/" + path;
      // Never write outside the chosen folder (a "../.." path in a reply).
      const std::string canonicalFolder = fs::weakly_canonical(folder, ec).string();
      const std::string canonicalPath = fs::weakly_canonical(path, ec).string();
      if (block.path[0] != '/' && !core::startsWith(canonicalPath, canonicalFolder + "/")) continue;
      bool ok = false;
      const std::string before = fs::is_regular_file(path, ec) ? core::readFile(path, &ok) : std::string();
      const bool isNew = !ok;
      if (!isNew && before == block.code) {
        ++unchanged;
        continue;
      }
      std::vector<diffview::FileDiff> parsed = parseDiff(diffview::generate(block.path, before, block.code));
      if (parsed.empty() || parsed.front().hunks.empty()) {
        ++unchanged;
        continue;
      }
      paths.push_back(path);
      created.push_back(isNew);
      diffs.push_back(parsed.front());
    }
    async::post([paths, created, diffs, unchanged, blocks] {
      if (paths.empty()) {
        widgets::toastInfo(unchanged > 0 ? "Every named file already matches the reply" : "Nothing to apply");
        return;
      }
      if (unchanged > 0) widgets::toastInfo(std::to_string(unchanged) + " file(s) already match and were skipped");
      loadApply(paths, created, diffs);
    });
  });
}

// The test loop writes files itself (backups beside them); this makes that
// write the "last apply" so Undo and the journal cover it.
void App::recordTestLoopApply(const std::vector<std::string>& relPaths) {
  const std::string folder = currentProjectFolder(*this);
  std::vector<AppliedFile> files;
  for (const std::string& rel : relPaths) {
    const std::string full = folder + "/" + rel;
    std::error_code ec;
    files.push_back({full, !fs::exists(full + ".bak", ec)});
  }
  if (files.empty()) return;
  journalApply(files);
  gLastApply = files;
}

std::vector<std::string> App::lastAppliedPaths() const {
  std::vector<std::string> out;
  for (const AppliedFile& f : gLastApply) out.push_back(f.path);
  return out;
}

void App::undoLastApply() {
  if (gLastApply.empty()) {
    widgets::toastInfo("Nothing to undo - no apply in this session");
    return;
  }
  const std::vector<AppliedFile> files = gLastApply;
  async::run([files] {
    const std::string failed = rollback(files);
    async::post([failed, files] {
      if (!failed.empty()) {
        widgets::toastError("Could not put back " + failed);
        return;
      }
      gLastApply.clear();
      widgets::toast(widgets::ToastLevel::Success, "Undid the last apply (" + std::to_string(files.size()) +
                                                       (files.size() == 1 ? " file)" : " files)"));
    });
  });
}

// ====================================================== folder context

void App::attachFolderAsContext(const std::string& folder) {
  widgets::toastInfo("Reading " + core::baseName(folder) + "...");
  async::run([this, folder] {
    const std::vector<WalkEntry> entries = walkFolder(folder, 64, 100000);
    std::string doc = "# Folder: " + folder + "\n\n";
    int included = 0, skippedBinary = 0, skippedLarge = 0, skippedCap = 0;
    for (const WalkEntry& entry : entries) {
      if (entry.dir) continue;
      if (entry.size > kMaxFileBytes) {
        ++skippedLarge;
        continue;
      }
      if (doc.size() + entry.size > kMaxFolderBytes) {
        ++skippedCap;
        continue;
      }
      bool ok = false;
      const std::string text = core::readFile(folder + "/" + entry.rel, &ok);
      if (!ok) continue;
      if (looksBinary(text)) {
        ++skippedBinary;
        continue;
      }
      std::string section = fencedFile(entry.rel, entry.rel, text);
      if (doc.size() + section.size() > kMaxFolderBytes) {
        ++skippedCap;
        continue;
      }
      doc += section;
      ++included;
    }
    if (skippedCap > 0) {
      doc += "_" + std::to_string(skippedCap) + " more file(s) left out to stay under the size cap._\n";
    }
    async::post([this, doc, included, skippedBinary, skippedLarge, skippedCap, folder] {
      if (included == 0) {
        widgets::toastInfo("No text files to attach in " + core::baseName(folder));
        return;
      }
      attachBytes("folder-context.md", "text/markdown", doc);
      std::string note = "Attached " + std::to_string(included) + " files (" +
                         core::humanBytes(static_cast<int64_t>(doc.size())) + ")";
      const int skipped = skippedBinary + skippedLarge + skippedCap;
      if (skipped > 0) note += ", skipped " + std::to_string(skipped);
      widgets::toastInfo(note);
    });
  });
}

void App::attachGitContext(const std::string& folder, const std::string& mode) {
  async::run([this, folder, mode] {
    native::ProcessResult probe =
        native::runProcess({"git", "rev-parse", "--is-inside-work-tree"}, folder, 10, false);
    if (probe.exitCode != 0 || core::trim(probe.output) != "true") {
      async::post([folder] { widgets::toastInfo(core::baseName(folder) + " is not a git repository"); });
      return;
    }
    std::vector<std::string> argv;
    std::string name;
    if (mode == "staged") {
      argv = {"git", "--no-pager", "diff", "--staged", "--no-color"};
      name = "git-staged.txt";
    } else if (mode == "log") {
      argv = {"git", "--no-pager", "log", "-8", "--stat", "--no-color"};
      name = "git-log.txt";
    } else {
      argv = {"git", "--no-pager", "diff", "--no-color"};
      name = "git-diff.txt";
    }
    native::ProcessResult res = native::runProcess(argv, folder, 30, false);
    async::post([this, res, name, mode] {
      if (res.exitCode != 0) {
        widgets::toastError("git failed: " + core::trim(res.output));
      } else if (core::trim(res.output).empty()) {
        widgets::toastInfo(mode == "staged" ? "Nothing staged" : "No uncommitted changes");
      } else {
        // .txt so the server's text loader takes it whatever its extension list.
        attachBytes(name, "text/plain", res.output);
      }
    });
  });
}

// ======================================================= build errors

void App::explainBuildError(const std::string& log) {
  if (core::trim(log).empty()) {
    widgets::toastInfo("Paste a build log first");
    return;
  }
  const std::string folder = currentProjectFolder(*this);
  pane = Pane::Chat;
  async::run([this, log, folder] {
    struct Ref {
      std::string path;
      int line;
    };
    std::vector<Ref> refs;
    std::set<std::string> seen;
    auto addRef = [&](const std::string& path, const std::string& line) {
      const std::string key = path + ":" + line;
      if (seen.count(key) || refs.size() >= 8) return;
      seen.insert(key);
      refs.push_back({path, std::atoi(line.c_str())});
    };
    // clang/gcc/go/rust/tsc `path:line(:col)`, MSVC `path(line,col)`, Python tracebacks.
    static const std::regex unixRef(R"(((?:[A-Za-z]:)?[\w.\-+/~]*[\w\-+]\.[A-Za-z0-9+]{1,8}):(\d+)(?::\d+)?)");
    static const std::regex msvcRef(R"(([\w.\-+/\\:]+\.[A-Za-z0-9+]{1,8})\((\d+)(?:,\d+)?\))");
    static const std::regex pyRef(R"re(File "([^"]+)", line (\d+))re");
    for (const std::regex* re : {&pyRef, &unixRef, &msvcRef}) {
      for (std::sregex_iterator it(log.begin(), log.end(), *re), end; it != end; ++it) {
        addRef((*it)[1].str(), (*it)[2].str());
      }
    }

    // Lazily listed so relative paths from a build dir can still be found by suffix.
    std::vector<WalkEntry> tree;
    bool treeListed = false;
    auto resolve = [&](const std::string& raw) -> std::string {
      std::error_code ec;
      if (!raw.empty() && raw[0] == '/' && fs::is_regular_file(raw, ec)) return raw;
      if (folder.empty()) return "";
      std::string rel = raw;
      while (core::startsWith(rel, "./")) rel.erase(0, 2);
      if (fs::is_regular_file(folder + "/" + rel, ec)) return folder + "/" + rel;
      if (!treeListed) {
        tree = walkFolder(folder, 12, 20000);
        treeListed = true;
      }
      while (core::startsWith(rel, "../")) rel.erase(0, 3);
      for (const WalkEntry& entry : tree) {
        if (!entry.dir && (entry.rel == rel || core::endsWith(entry.rel, "/" + rel))) {
          return folder + "/" + entry.rel;
        }
      }
      return "";
    };

    std::string excerpts;
    int found = 0;
    for (const Ref& ref : refs) {
      const std::string path = resolve(ref.path);
      if (path.empty()) continue;
      bool ok = false;
      const std::string text = core::readFile(path, &ok);
      if (!ok || looksBinary(text)) continue;
      const std::vector<std::string> lines = core::split(text, '\n');
      const int first = std::max(1, ref.line - 25);
      const int last = std::min(static_cast<int>(lines.size()), ref.line + 25);
      if (first > last) continue;
      std::string body;
      for (int n = first; n <= last; ++n) {
        char prefix[16];
        snprintf(prefix, sizeof(prefix), "%s%5d  ", n == ref.line ? ">" : " ", n);
        body += prefix + lines[n - 1] + "\n";
      }
      excerpts += fencedFile(ref.path + " (lines " + std::to_string(first) + "-" + std::to_string(last) +
                                 ", error at line " + std::to_string(ref.line) + ")",
                             path, body);
      ++found;
    }

    // Compilers put the root cause first and the summary last; keep both ends.
    std::string trimmed = log;
    if (trimmed.size() > 12000) {
      trimmed = trimmed.substr(0, 7000) + "\n...[" + std::to_string(trimmed.size() - 12000) +
                " characters omitted]...\n" + trimmed.substr(trimmed.size() - 5000);
    }
    std::string prompt =
        "This build failed. Explain the root cause in plain terms, then give the smallest fix "
        "as code (a unified diff where possible).\n\n" +
        std::string("Build output:\n") + fenced("text", trimmed);
    if (found > 0) prompt += "\nThe source around each reported location (`>` marks the line):\n\n" + excerpts;
    async::post([this, prompt] { sendMessage(prompt); });
  });
}

// ========================================================= project mode

void App::setProjectFolder(const std::string& folder) {
  if (folder != projectFolder) {
    projectFiles.clear();
    projectTree.clear();
    gTreeFolder.clear();
  }
  projectFolder = folder;
  gProjectLoaded = true;
  settings().extra["projectFolder"] = folder;
  settings().extra["projectFiles"] = projectFiles;
  settings().save();
}

void App::buildRepoMap() {
  const std::string folder = currentProjectFolder(*this);
  if (folder.empty() || gRepoMapBuilding) return;
  gRepoMapBuilding = true;
  repoMapStatus = "Indexing...";
  async::run([this, folder] {
    std::vector<repomap::FileMap> maps;
    size_t symbols = 0;
    for (const WalkEntry& entry : walkFolder(folder, 12, 6000)) {
      if (entry.dir || entry.size > 512 * 1024) continue;
      const std::string language = repomap::languageOf(entry.rel);
      if (language.empty()) continue;
      bool ok = false;
      const std::string text = core::readFile(folder + "/" + entry.rel, &ok);
      if (!ok || looksBinary(text)) continue;
      repomap::FileMap map{entry.rel, language, repomap::extract(language, text)};
      symbols += map.symbols.size();
      maps.push_back(map);
      if (maps.size() >= 3000) break;
    }
    const std::string rendered = repomap::render(maps, kRepoMapChars);
    const size_t files = std::count_if(maps.begin(), maps.end(), [](const repomap::FileMap& m) { return !m.symbols.empty(); });
    async::post([this, folder, maps, symbols, files, rendered] {
      gRepoMapBuilding = false;
      if (folder != currentProjectFolder(*this)) return;
      gRepoMap = maps;
      gRepoMapFolder = folder;
      char buf[128];
      snprintf(buf, sizeof(buf), "%zu symbols in %zu files (~%.1fk tokens sent)", symbols, files, rendered.size() / 4000.0);
      repoMapStatus = buf;
    });
  });
}

std::string App::repoMapBlock() const {
  if (!core::Settings::instance().extra.value("projectRepoMap", true)) return "";
  if (gRepoMapFolder != currentProjectFolder(*this) || gRepoMap.empty()) return "";
  return repomap::render(gRepoMap, kRepoMapChars);
}

std::string App::projectContextBlock() const {
  const std::string folder = currentProjectFolder(*this);
  if (folder.empty()) return "";
  std::vector<std::string> files = projectFiles;
  if (files.empty() && projectFolder.empty()) {
    const Json& extra = core::Settings::instance().extra;
    if (extra.contains("projectFiles") && extra["projectFiles"].is_array()) {
      for (const auto& f : extra["projectFiles"]) {
        if (f.is_string()) files.push_back(f.get<std::string>());
      }
    }
  }

  // The tree is depth 3 / 400 entries: enough to orient the model without
  // spending the context window on it.
  std::vector<std::string> tree;
  if (gTreeFolder == folder && !projectTree.empty()) {
    for (const std::string& rel : projectTree) {
      const std::string bare = core::endsWith(rel, "/") ? rel.substr(0, rel.size() - 1) : rel;
      if (std::count(bare.begin(), bare.end(), '/') < 3) tree.push_back(rel);
      if (tree.size() >= 400) break;
    }
  } else {
    for (const WalkEntry& entry : walkFolder(folder, 3, 400)) tree.push_back(entry.rel + (entry.dir ? "/" : ""));
  }

  std::string out = "You are working in the project at " + folder + ".\n\nFile tree:\n```\n";
  for (const std::string& rel : tree) {
    const std::string bare = core::endsWith(rel, "/") ? rel.substr(0, rel.size() - 1) : rel;
    const auto depth = std::count(bare.begin(), bare.end(), '/');
    out += std::string(static_cast<size_t>(depth) * 2, ' ') + core::baseName(bare) + (core::endsWith(rel, "/") ? "/" : "") + "\n";
  }
  if (tree.size() >= 400) out += "...\n";
  out += "```\n";

  // The repo map: every symbol the project defines, so the model can name a
  // function or ask for a file instead of guessing from the tree.
  const std::string map = repoMapBlock();
  if (!map.empty()) out += "\n" + map;

  size_t budget = kMaxProjectFileBytes;
  for (const std::string& rel : files) {
    bool ok = false;
    const std::string text = core::readFile(folder + "/" + rel, &ok);
    if (!ok || looksBinary(text)) continue;
    if (text.size() > budget) {
      out += "\n(" + rel + " left out: over the project context budget)\n";
      continue;
    }
    budget -= text.size();
    out += "\n" + fencedFile(rel, rel, text);
  }
  return out;
}

void App::drawProjectFilesPopup() {
  // Project folder and ticked files persist in settings; pick them up once.
  if (gProjectLoaded && !projectFolder.empty() && gRepoMapFolder != projectFolder && !gRepoMapBuilding &&
      settings().extra.value("projectRepoMap", true)) {
    buildRepoMap();
  }
  if (!gProjectLoaded) {
    gProjectLoaded = true;
    const Json& extra = settings().extra;
    if (projectFolder.empty() && extra.contains("projectFolder") && extra["projectFolder"].is_string()) {
      projectFolder = extra["projectFolder"].get<std::string>();
    }
    if (projectFiles.empty() && extra.contains("projectFiles") && extra["projectFiles"].is_array()) {
      for (const auto& f : extra["projectFiles"]) {
        if (f.is_string()) projectFiles.push_back(f.get<std::string>());
      }
    }
  }

  // The apply dialog needs a once-per-frame host; this function is it.
  drawApplyModal();

  if (!showProjectFiles) return;
  if (!projectFolder.empty() && gTreeFolder != projectFolder && !gTreeLoading) {
    gTreeLoading = true;
    const std::string folder = projectFolder;
    async::run([this, folder] {
      std::vector<std::string> listing;
      for (const WalkEntry& entry : walkFolder(folder, 8, 4000)) {
        listing.push_back(entry.rel + (entry.dir ? "/" : ""));
      }
      async::post([this, folder, listing] {
        gTreeLoading = false;
        if (folder != projectFolder) return;
        projectTree = listing;
        gTreeFolder = folder;
      });
    });
  }

  ImGui::OpenPopup("Project files");
  ImGui::SetNextWindowSize(ImVec2(640, 600), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Project files", &showProjectFiles)) {
    if (projectFolder.empty()) {
      widgets::dimText("No project folder set.");
    } else {
      ImGui::TextUnformatted(projectFolder.c_str());
    }
    if (ImGui::Button(projectFolder.empty() ? "Choose folder..." : "Change folder...")) {
      auto picked = native::chooseFolder();
      if (!picked.empty()) setProjectFolder(picked.front());
    }
    if (!projectFolder.empty()) {
      ImGui::SameLine();
      if (widgets::ghostButton("Refresh")) gTreeFolder.clear();
      ImGui::SameLine();
      if (widgets::ghostButton("Untick all")) {
        projectFiles.clear();
        settings().extra["projectFiles"] = projectFiles;
        settings().save();
      }
    }
    widgets::dimText("Ticked files are sent in full with every message, after the file tree.");
    {
      bool useMap = settings().extra.value("projectRepoMap", true);
      if (ImGui::Checkbox("Send a repo map (every symbol the project defines, file:line) instead of guessing from the tree", &useMap)) {
        settings().extra["projectRepoMap"] = useMap;
        settings().save();
      }
      if (useMap && !projectFolder.empty()) {
        if (gRepoMapFolder != projectFolder && !gRepoMapBuilding) buildRepoMap();
        ImGui::SameLine();
        if (widgets::ghostButton(gRepoMapBuilding ? "Indexing..." : "Reindex")) buildRepoMap();
        if (!repoMapStatus.empty()) widgets::dimText("Repo map: %s", repoMapStatus.c_str());
      }
    }
    widgets::searchField("##projfilter", &gProjectFilter, "Filter files");
    ImGui::Text("%d file(s) ticked", static_cast<int>(projectFiles.size()));
    ImGui::Separator();

    auto fileCheckbox = [this](const std::string& rel, const std::string& label) {
      auto it = std::find(projectFiles.begin(), projectFiles.end(), rel);
      bool ticked = it != projectFiles.end();
      if (ImGui::Checkbox((label + "##" + rel).c_str(), &ticked)) {
        if (ticked) projectFiles.push_back(rel);
        else projectFiles.erase(it);
        settings().extra["projectFiles"] = projectFiles;
        settings().save();
      }
    };

    ImGui::BeginChild("##projtree", ImVec2(0, -36));
    if (gTreeLoading && projectTree.empty()) {
      widgets::loadingRow("Listing files...");
    } else if (!gProjectFilter.empty()) {
      for (const std::string& rel : projectTree) {
        if (core::endsWith(rel, "/") || !core::containsNoCase(rel, gProjectFilter)) continue;
        fileCheckbox(rel, rel);
      }
    } else {
      // projectTree is a pre-order listing, so the open/closed state of each
      // directory decides which following entries are visible.
      int openDepth = 0;
      int collapsedAt = INT_MAX;
      for (const std::string& rel : projectTree) {
        const bool isDir = core::endsWith(rel, "/");
        const std::string bare = isDir ? rel.substr(0, rel.size() - 1) : rel;
        const int depth = static_cast<int>(std::count(bare.begin(), bare.end(), '/'));
        if (depth > collapsedAt) continue;
        collapsedAt = INT_MAX;
        while (openDepth > depth) {
          ImGui::TreePop();
          --openDepth;
        }
        if (isDir) {
          if (ImGui::TreeNode(bare.c_str(), "%s/", core::baseName(bare).c_str())) ++openDepth;
          else collapsedAt = depth;
        } else {
          fileCheckbox(rel, core::baseName(bare));
        }
      }
      while (openDepth-- > 0) ImGui::TreePop();
    }
    ImGui::EndChild();
    if (widgets::primaryButton("Done")) showProjectFiles = false;
    ImGui::EndPopup();
  }
}

// ======================================================= command runner

void App::runCommand(const std::string& command) {
  if (core::trim(command).empty()) return;
  if (commandRunning) {
    widgets::toastInfo("A command is already running");
    return;
  }
  const std::string folder = currentProjectFolder(*this);
  const std::string workdir = folder.empty() ? native::homeDirectory() : folder;
  commandRunning = true;
  commandOutput += "$ " + command + "\n";
  async::run([this, command, workdir] {
    // A login shell picks up the user's PATH (Homebrew etc.), which a GUI
    // app does not inherit.
    native::ProcessResult res =
        native::runProcess({"/bin/zsh", "-l", "-c", command}, workdir, kCommandTimeoutSeconds, false);
    async::post([this, res] {
      commandRunning = false;
      commandOutput += res.output;
      if (!res.output.empty() && res.output.back() != '\n') commandOutput += "\n";
      if (res.timedOut) {
        commandOutput += "[timed out after " + std::to_string(kCommandTimeoutSeconds) + " s]\n";
      } else {
        commandOutput += "[exit " + std::to_string(res.exitCode) + "]\n";
      }
      if (commandOutput.size() > kMaxCommandOutput) {
        commandOutput.erase(0, commandOutput.size() - kMaxCommandOutput);
      }
    });
  });
}

// ===================================================== code block actions

CodeResult* App::codeResultFor(const std::string& key) {
  auto it = codeResults.find(key);
  return it == codeResults.end() ? nullptr : &it->second;
}

void App::drawCodeBlockActions(const std::string& code, const std::string& language) {
  const std::string key = codeKey(code);
  const std::string lintKey = "lint:" + key;
  ImGui::PushID(key.c_str());

  const CodeResult* run = codeResultFor(key);
  const CodeResult* lint = codeResultFor(lintKey);
  const bool runnable = canRun(language, false);
  const bool lintable = canRun(language, true);

  // One compact row: this sits under every code block in the chat.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
  if (isArtifactLanguage(language, code)) {
    if (widgets::ghostButton("Preview")) openArtifact(code, language);
    ImGui::SameLine();
  }
  // Plugin renderers for this block's language open their HTML in the artifact pane.
  for (const plugins::Renderer& renderer : plugins::Host::instance().renderers()) {
    if (renderer.language != core::lower(language)) continue;
    ImGui::PushID(renderer.pluginId.c_str());
    if (widgets::ghostButton(renderer.label.c_str())) {
      std::string error;
      const std::string html = plugins::Host::instance().render(renderer.pluginId, language, code, &error);
      if (error.empty()) openArtifact(html, "html");
      else widgets::toastError(renderer.pluginName + ": " + error);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rendered by the %s plugin", renderer.pluginName.c_str());
    ImGui::PopID();
    ImGui::SameLine();
  }
  if (playground::kindFor(language, code) != playground::Kind::None) {
    if (widgets::ghostButton("Try it")) openPlayground(code, language);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run this %s locally against sample data, a file or your own input", playground::kindName(playground::kindFor(language, code)));
    ImGui::SameLine();
  }
  const bool runBusy = !runnable || (run && run->running);
  widgets::beginDisabled(runBusy);
  if (widgets::ghostButton("Run")) runCodeBlock(code, language);
  widgets::endDisabled(runBusy);
  if (!runnable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("No local runner for %s", language.empty() ? "untagged blocks" : language.c_str());
  }
  ImGui::SameLine();
  const bool lintBusy = !lintable || (lint && lint->running);
  widgets::beginDisabled(lintBusy);
  if (widgets::ghostButton("Lint")) lintCodeBlock(code, language);
  widgets::endDisabled(lintBusy);
  ImGui::SameLine();
  if (widgets::ghostButton("Apply to file...")) applyCodeBlockToFile(code);
  ImGui::SameLine();
  if (widgets::ghostButton("Copy")) {
    platform::setClipboardText(code);
    widgets::toastInfo("Copied");
  }
  ImGui::PopStyleVar(2);

  const auto& palette = theme::palette();
  for (const auto& [resultKey, isLint] : {std::pair<std::string, bool>{key, false}, {lintKey, true}}) {
    CodeResult* r = codeResultFor(resultKey);
    if (!r) continue;
    ImGui::PushID(isLint ? "lint" : "run");
    ImU32 color = palette.textDim;
    if (!r->running) color = r->timedOut ? palette.warning : (r->exitCode == 0 ? palette.success : palette.danger);
    if (r->running) {
      widgets::spinner("##spin", 6.0f, 2.0f);
      ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(color));
    ImGui::TextUnformatted(resultBoxTitle(*r, isLint).c_str());
    ImGui::PopStyleColor();
    bool cleared = false;
    if (!r->running) {
      ImGui::SameLine();
      cleared = widgets::ghostButton("Clear");
    }
    if (!r->running && !r->output.empty() && !cleared) {
      const int lines = static_cast<int>(std::count(r->output.begin(), r->output.end(), '\n')) + 1;
      theme::pushMono();
      const float height = std::min(lines, 14) * ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2 + 4;
      // Read-only input so the output can be selected and copied.
      ImGui::InputTextMultiline("##output", &r->output, ImVec2(-1, height), ImGuiInputTextFlags_ReadOnly);
      theme::pop();
    }
    ImGui::PopID();
    if (cleared) codeResults.erase(resultKey);
  }
  ImGui::PopID();
}

}  // namespace app
