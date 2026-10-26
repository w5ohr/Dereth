#include "onboarding.h"

#include <algorithm>

#include "common.h"

namespace onboarding {

const std::vector<TourStep>& tourSteps() {
  static const std::vector<TourStep> steps{
      {"welcome", "Welcome to Open WebUI Desktop", "A native client for your Open WebUI server: chats, notes, knowledge and tools, all in one window, with a lot that runs on this Mac alone. This tour takes a minute.", "none"},
      {"rail", "The rail", "Panes live on the left: chats, notes, channels, workspace, images, files, memories, the library of saved answers, tools and settings. Cmd+1 to Cmd+0 jump between them.", "rail"},
      {"sidebar", "Your chats", "Every chat, folders, pinned and archived ones, and a search box that can look by meaning. Private chats (the lock) stay on this Mac and talk to Ollama directly.", "sidebar"},
      {"composer", "The composer", "Type, paste or drop files and images. Enter sends, Shift+Enter breaks a line. The row of buttons attaches files, starts voice, picks tools and more.", "composer"},
      {"header", "The chat header", "Pick models, compare sibling answers, open the conversation tree, pinned messages, what was sent, and the presets.", "header"},
      {"palette", "The command palette", "Cmd+K finds every command, pane, model, prompt, chat and preset. When in doubt, start there.", "palette"},
      {"tools", "Tools", "Run commands, index a folder for local search, connect MCP servers, watch traffic and health, evaluate models, and more - all under Tools.", "tools"},
      {"settings", "Settings", "Themes, shortcuts, voice, the local index, smart folders, budgets, backups: everything configurable is in Settings. Cmd+/ shows the shortcut cheat sheet any time.", "settings"},
  };
  return steps;
}

State fromJson(const Json& extra) {
  State s;
  if (!extra.is_object()) return s;
  const Json j = extra.value("onboarding", Json::object());
  if (!j.is_object()) return s;
  s.tourDone = j.value("tourDone", false);
  s.tourStep = std::clamp(j.value("tourStep", 0), 0, static_cast<int>(tourSteps().size()) - 1);
  if (j.contains("lastSeenVersion") && j["lastSeenVersion"].is_string()) s.lastSeenVersion = j["lastSeenVersion"].get<std::string>();
  return s;
}

Json toJson(const State& s) { return Json{{"tourDone", s.tourDone}, {"tourStep", s.tourStep}, {"lastSeenVersion", s.lastSeenVersion}}; }

const std::vector<Release>& releases() {
  static const std::vector<Release> list{
      {"1.1", "2026-10-25",
       {"Theme editor with VS Code and Base16 import and a per-server accent colour",
        "Shortcut editor with conflict detection, a cheat sheet on Cmd+/, and optional Vim keys in the transcript",
        "Hybrid local search: exact identifiers are found through BM25 beside the vector index",
        "Smart folders and tags: rules file chats automatically; the small model tags the rest",
        "Library of saved answers across every chat, with notes, tags and Markdown export",
        "Model evaluation: prompt sets across models, graded by a judge model, trended over runs",
        "Playgrounds: try a reply's regex, SQL or jq locally",
        "Retrieval inspector: see, pin or exclude retrieved chunks and re-ask",
        "Traffic inspector with HAR export, replay and curl",
        "Read aloud with the Mac's own voices when the server has no text-to-speech",
        "Cited export (Markdown footnotes, BibTeX), the editor bridge, low-power mode, message selection, model warmup, token budgets, the model library, health dashboard, Obsidian vault sync, memory proposals, semantic chat search, script and browser tools, native document chat, direct backends, math and diagrams"}},
      {"1.0", "2026-10-24", {"First release: chats, notes, channels, workspace, images, files, memories, admin, API console, tools, private chats, redaction, audit log, backups and more"}},
  };
  return list;
}

int compareVersion(const std::string& a, const std::string& b) {
  const std::vector<std::string> pa = core::split(a, '.'), pb = core::split(b, '.');
  const size_t n = std::max(pa.size(), pb.size());
  for (size_t i = 0; i < n; ++i) {
    const int x = i < pa.size() ? atoi(pa[i].c_str()) : 0;
    const int y = i < pb.size() ? atoi(pb[i].c_str()) : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

std::vector<Release> pendingReleases(const std::string& lastSeen, const std::string& current) {
  std::vector<Release> out;
  for (const Release& r : releases()) {
    if (compareVersion(r.version, current) > 0) continue;
    if (!lastSeen.empty() && compareVersion(r.version, lastSeen) <= 0) continue;
    out.push_back(r);
  }
  return out;
}

std::string whatsNewText(const std::vector<Release>& list) {
  std::string out;
  for (const Release& r : list) {
    out += "Version " + r.version + " - " + r.date + "\n";
    for (const std::string& item : r.items) out += "  - " + item + "\n";
    out += "\n";
  }
  return core::trim(out);
}

}  // namespace onboarding
