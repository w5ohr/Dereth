// Triggers: things that run a recipe on their own. A watched folder runs its
// recipe for every file that lands there (the file is attached to the run,
// its path is the recipe's {{input:File}}); the clipboard watcher runs its
// recipe whenever new text is copied (the steps read it with {{clipboard}}).
// Configuration lives in settings extra["triggers"]; a run queues behind a
// recipe already going.
#include "app.h"

#include <sys/stat.h>

#include <deque>
#include <set>

#include "core/async.h"
#include "core/common.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct FolderWatch {
  std::string path;
  std::string recipeId;
  bool enabled = true;
  std::string extensions;       // "pdf,md,txt" or "" for any
  int token = -1;
};

struct Pending {
  std::string path;
  std::string recipeId;
  double readyAt = 0.0;         // settle time: the file may still be written
};

struct TriggerState {
  std::vector<FolderWatch> folders;
  std::deque<Pending> queue;
  std::set<std::string> seen;   // paths already handled (per session)
  std::string lastClipboard;
  double lastClipboardCheck = 0.0;
  std::string lastFired;        // for the pane
  bool started = false;
};
TriggerState g;

std::string extensionOf(const std::string& path) {
  const std::string base = core::baseName(path);
  const size_t dot = base.rfind('.');
  return dot == std::string::npos ? "" : core::lower(base.substr(dot + 1));
}

bool extensionAllowed(const std::string& list, const std::string& ext) {
  if (core::trim(list).empty()) return true;
  for (const std::string& item : core::split(list, ',')) {
    if (core::lower(core::trim(item)) == ext) return true;
  }
  return false;
}

Json& config() {
  Json& t = core::Settings::instance().extra["triggers"];
  if (!t.is_object()) t = Json::object();
  if (!t.contains("folders") || !t["folders"].is_array()) t["folders"] = Json::array();
  if (!t.contains("clipboard") || !t["clipboard"].is_object()) t["clipboard"] = Json::object();
  return t;
}

}  // namespace

void App::triggersInit() {
  // Stop whatever ran before, then start each enabled folder watch.
  for (FolderWatch& w : g.folders) {
    if (w.token >= 0) native::stopWatching(w.token);
  }
  g.folders.clear();
  const Json cfg = config();
  for (const auto& f : cfg["folders"]) {
    if (!f.is_object()) continue;
    FolderWatch w;
    w.path = f.value("path", "");
    w.recipeId = f.value("recipe", "");
    w.enabled = f.value("enabled", true);
    w.extensions = f.value("extensions", "");
    if (w.path.empty()) continue;
    if (w.enabled && !w.recipeId.empty()) {
      const std::string recipeId = w.recipeId, extensions = w.extensions, folder = w.path;
      w.token = native::watchFolder(w.path, [this, recipeId, extensions, folder](const std::vector<std::string>& paths) {
        async::post([this, paths, recipeId, extensions, folder] {
          for (const std::string& path : paths) {
            const std::string base = core::baseName(path);
            if (base.empty() || base[0] == '.' || core::startsWith(base, "~$")) continue;
            if (!core::startsWith(path, folder)) continue;
            struct stat st {};
            if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            if (!extensionAllowed(extensions, extensionOf(path))) continue;
            if (g.seen.count(path)) continue;
            g.seen.insert(path);
            g.queue.push_back({path, recipeId, core::nowMillis() + 2500.0});
          }
        });
      });
    }
    g.folders.push_back(w);
  }
  g.started = true;
}

void App::triggersShutdown() {
  for (FolderWatch& w : g.folders) {
    if (w.token >= 0) native::stopWatching(w.token);
  }
  g.folders.clear();
}

void App::tickTriggers() {
  if (!g.started || !signedIn_) return;
  const double now = core::nowMillis();

  // Folder queue: one run at a time, after the file has settled.
  if (!g.queue.empty() && !recipeRunning()) {
    Pending& p = g.queue.front();
    if (now >= p.readyAt) {
      struct stat st {};
      const bool exists = stat(p.path.c_str(), &st) == 0;
      // nowMillis is a steady clock, so the settle test uses wall time.
      const bool settled = exists && st.st_mtime < static_cast<time_t>(core::nowSeconds()) - 1;
      if (!exists) {
        g.queue.pop_front();
      } else if (settled) {
        const Pending job = p;
        g.queue.pop_front();
        g.lastFired = core::baseName(job.path) + " -> recipe";
        std::map<std::string, std::string> inputs;
        inputs["File"] = job.path;
        runRecipeWith(job.recipeId, inputs, {job.path});
        widgets::toastInfo("Watched folder: running the recipe for " + core::baseName(job.path));
      } else {
        p.readyAt = now + 1500.0;
      }
    }
  }

  // Clipboard watcher: new text, long enough, not one of our own replies.
  const Json cfg = config();
  const Json& clip = cfg["clipboard"];
  if (clip.value("enabled", false) && !clip.value("recipe", "").empty() && now - g.lastClipboardCheck > 1000.0) {
    g.lastClipboardCheck = now;
    const std::string text = platform::clipboardText();
    if (text != g.lastClipboard) {
      const bool first = g.lastClipboard.empty() && !g.seen.count("clipboard-primed");
      g.lastClipboard = text;
      g.seen.insert("clipboard-primed");
      const int minChars = std::max(1, clip.value("minChars", 20));
      bool ours = false;
      for (const auto& [id, m] : chat.messages) {
        if (!text.empty() && (m.content == text || m.content.find(text) != std::string::npos)) ours = true;
      }
      if (!first && !ours && static_cast<int>(core::trim(text).size()) >= minChars && !recipeRunning()) {
        g.lastFired = "clipboard (" + std::to_string(text.size()) + " chars) -> recipe";
        runRecipeWith(clip.value("recipe", ""), {}, {});
        widgets::toastInfo("Clipboard watcher: running the recipe");
      }
    }
  }
}

void App::drawTriggersSection(float width) {
  (void)width;
  const auto& palette = theme::palette();
  widgets::sectionHeader("TRIGGERS");
  widgets::dimText("Run a recipe without asking: when a file lands in a folder (it is attached, and its path is the\n"
                   "recipe's {{input:File}}), or when new text is copied (the steps read it with {{clipboard}}).");
  const std::vector<recipes::Recipe> list = recipeList();
  auto recipeCombo = [&](const char* id, std::string* recipeId) {
    std::string label = "(choose a recipe)";
    for (const recipes::Recipe& r : list) {
      if (r.id == *recipeId) label = r.name;
    }
    bool changed = false;
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo(id, label.c_str())) {
      for (const recipes::Recipe& r : list) {
        if (ImGui::Selectable(r.name.c_str(), r.id == *recipeId)) {
          *recipeId = r.id;
          changed = true;
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  };

  Json& cfg = config();
  bool changed = false;
  // Folders.
  int remove = -1;
  for (size_t i = 0; i < cfg["folders"].size(); ++i) {
    Json& f = cfg["folders"][i];
    ImGui::PushID(static_cast<int>(i));
    bool enabled = f.value("enabled", true);
    if (ImGui::Checkbox("##on", &enabled)) {
      f["enabled"] = enabled;
      changed = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(core::baseName(f.value("path", "")).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.value("path", "").c_str());
    ImGui::SameLine();
    std::string recipeId = f.value("recipe", "");
    if (recipeCombo("##recipe", &recipeId)) {
      f["recipe"] = recipeId;
      changed = true;
    }
    ImGui::SameLine();
    std::string ext = f.value("extensions", "");
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputTextWithHint("##ext", "pdf,md,txt (any)", &ext)) {
      f["extensions"] = ext;
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
    ImGui::PopID();
  }
  if (remove >= 0) {
    cfg["folders"].erase(cfg["folders"].begin() + remove);
    changed = true;
  }
  if (ImGui::SmallButton("Watch a folder...")) {
    const std::vector<std::string> picked = native::chooseFolder();
    if (!picked.empty()) {
      cfg["folders"].push_back(Json{{"path", picked.front()}, {"recipe", list.empty() ? "" : list.front().id}, {"enabled", true}, {"extensions", ""}});
      changed = true;
    }
  }
  // Clipboard.
  ImGui::Dummy(ImVec2(0, 4));
  Json& clip = cfg["clipboard"];
  bool clipOn = clip.value("enabled", false);
  if (ImGui::Checkbox("Clipboard watcher", &clipOn)) {
    clip["enabled"] = clipOn;
    changed = true;
  }
  ImGui::SameLine();
  std::string clipRecipe = clip.value("recipe", "");
  if (recipeCombo("##cliprecipe", &clipRecipe)) {
    clip["recipe"] = clipRecipe;
    changed = true;
  }
  ImGui::SameLine();
  int minChars = clip.value("minChars", 20);
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("min chars", &minChars)) {
    clip["minChars"] = std::max(1, minChars);
    changed = true;
  }
  if (clipOn) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.warning));
    ImGui::TextUnformatted("On: every copy of new text (outside this app's replies) starts the recipe.");
    ImGui::PopStyleColor();
  }
  if (!g.lastFired.empty()) widgets::dimText("Last trigger: %s", g.lastFired.c_str());
  if (!g.queue.empty()) widgets::dimText("%zu file(s) waiting", g.queue.size());
  if (changed) {
    settings().save();
    triggersInit();
  }
}

}  // namespace app
