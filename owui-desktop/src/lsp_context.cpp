// LSP context in the app (#97): language servers started per project folder,
// symbols named in the question looked up (workspace symbols first, then the
// files that mention the name opened and their document symbols read), and
// the hover text plus the definition's source added to the request as a
// system message. Settings card with the servers found and a try-it box.
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <regex>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/lsp.h"
#include "core/settings.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;

namespace app {
namespace {

std::mutex gSessionsMutex;   // also serialises lookups: one at a time
std::map<std::string, std::unique_ptr<lsp::Client>> gSessions;
std::mutex gNoteMutex;
std::string gLastNote;
std::string gTryQuestion;
std::string gTryResult;
std::string gTryLog;
bool gTrying = false;

struct Prefs {
  bool enabled = false;
  int maxSymbols = 4;
};
Prefs prefs() {
  const Json j = core::Settings::instance().extra.value("lspContext", Json::object());
  Prefs p;
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", false);
  p.maxSymbols = std::clamp(j.value("maxSymbols", 4), 1, 8);
  return p;
}

bool skipDir(const std::string& name) {
  static const char* skip[] = {".git", "node_modules", "third_party", "vendor", ".cache", "dist", "target", "venv", ".venv", "__pycache__", "DerivedData", ".build", "Pods", ".idea", ".vscode"};
  for (const char* s : skip) {
    if (name == s) return true;
  }
  return name.rfind("build", 0) == 0 || name.rfind("cmake-build", 0) == 0;
}

// Source files by server, skipping build output and vendored code.
std::map<std::string, std::vector<std::string>> sourceFilesByServer(const std::string& folder, const std::vector<lsp::ServerSpec>& servers) {
  std::map<std::string, std::vector<std::string>> out;
  std::error_code ec;
  size_t seen = 0;
  for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator() && seen < 8000; it.increment(ec)) {
    if (ec) break;
    ++seen;
    const std::string name = it->path().filename().string();
    if (it->is_directory(ec)) {
      if (skipDir(name) || (!name.empty() && name[0] == '.')) it.disable_recursion_pending();
      continue;
    }
    if (const lsp::ServerSpec* s = lsp::serverFor(servers, it->path().string())) out[s->name].push_back(it->path().string());
  }
  return out;
}

lsp::Client* session(const lsp::ServerSpec& spec, const std::string& folder, std::string* error) {
  const std::string key = spec.name + "|" + folder;
  auto it = gSessions.find(key);
  if (it != gSessions.end() && it->second && it->second->alive()) return it->second.get();
  auto client = std::make_unique<lsp::Client>();
  if (!client->start(spec, folder, error)) return nullptr;
  lsp::Client* raw = client.get();
  gSessions[key] = std::move(client);
  return raw;
}

// Files that mention `name`, ones that look like they define it first.
std::vector<std::string> filesMentioning(const std::vector<std::string>& files, const std::string& name, size_t max) {
  const std::regex word("\\b" + name + "\\b");
  const std::regex def("(\\b(class|struct|enum|union|def|fn|func|function|interface|namespace|type)\\s+" + name + "\\b)|(\\b" + name + "\\s*\\()");
  std::vector<std::string> defining, mentioning;
  size_t read = 0;
  for (const std::string& f : files) {
    if (read >= 2500) break;
    std::error_code ec;
    if (fs::file_size(f, ec) > (1u << 20)) continue;
    ++read;
    bool ok = false;
    const std::string text = core::readFile(f, &ok);
    if (!ok || text.find(name) == std::string::npos || !std::regex_search(text, word)) continue;
    (std::regex_search(text, def) ? defining : mentioning).push_back(f);
  }
  // Implementation files before headers among the defining ones: they hold the body.
  std::stable_sort(defining.begin(), defining.end(), [](const std::string& a, const std::string& b) {
    auto header = [](const std::string& p) { return p.size() > 2 && (p.compare(p.size() - 2, 2, ".h") == 0 || p.find(".hpp") != std::string::npos); };
    return !header(a) && header(b);
  });
  defining.insert(defining.end(), mentioning.begin(), mentioning.end());
  if (defining.size() > max) defining.resize(max);
  return defining;
}

}  // namespace

std::string App::lspLookup(const std::string& question, const std::string& folder, std::string* log) {
  std::lock_guard<std::mutex> lock(gSessionsMutex);
  const auto started = std::chrono::steady_clock::now();
  auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(); };
  const std::vector<lsp::Term> terms = lsp::termsFromQuestion(question);
  if (terms.empty()) {
    if (log) *log = "no code identifiers in the question";
    return "";
  }
  const std::vector<lsp::ServerSpec> servers = lsp::defaultServers();
  const auto files = sourceFilesByServer(folder, servers);
  std::vector<lsp::Entry> entries;
  std::vector<std::string> notes;
  const int maxSymbols = prefs().maxSymbols;
  for (const lsp::Term& term : terms) {
    if (static_cast<int>(entries.size()) >= maxSymbols) break;
    const std::string label = (term.qualifier.empty() ? "" : term.qualifier + "::") + term.name;
    bool found = false;
    for (const lsp::ServerSpec& spec : servers) {
      auto fit = files.find(spec.name);
      if (fit == files.end() || fit->second.empty()) continue;
      std::string error;
      lsp::Client* client = session(spec, folder, &error);
      if (!client) {
        notes.push_back(spec.name + ": " + error);
        continue;
      }
      std::vector<lsp::Symbol> candidates = lsp::fromWorkspaceSymbols(client->request("workspace/symbol", Json{{"query", term.name}}, 4000, &error));
      int idx = lsp::bestMatch(candidates, term);
      if (idx < 0) {
        for (const std::string& f : filesMentioning(fit->second, term.name, 3)) {
          if (!client->openFile(f, &error)) continue;
          for (const lsp::Symbol& s : lsp::flattenDocumentSymbols(client->request("textDocument/documentSymbol", Json{{"textDocument", Json{{"uri", lsp::pathToUri(f)}}}}, 15000, &error), f)) candidates.push_back(s);
        }
        idx = lsp::bestMatch(candidates, term);
      }
      if (idx < 0) continue;
      lsp::Entry e;
      e.symbol = candidates[static_cast<size_t>(idx)];
      // Workspace symbols locate only the name; the file's document symbols
      // carry the whole definition's range, so the body comes along.
      if (e.symbol.endLine - e.symbol.startLine < 1 && client->openFile(e.symbol.path, &error)) {
        const auto inFile = lsp::flattenDocumentSymbols(client->request("textDocument/documentSymbol", Json{{"textDocument", Json{{"uri", lsp::pathToUri(e.symbol.path)}}}}, 15000, &error), e.symbol.path);
        for (const lsp::Symbol& s : inFile) {
          if (lsp::matches(s, term) && s.line == e.symbol.line) {
            e.symbol.startLine = s.startLine;
            e.symbol.endLine = s.endLine;
            if (e.symbol.container.empty()) e.symbol.container = s.container;
            break;
          }
        }
      }
      e.server = spec.name;
      e.language = lsp::languageIdFor(e.symbol.path) == "objective-cpp" ? "objectivec" : lsp::languageIdFor(e.symbol.path);
      client->openFile(e.symbol.path, &error);
      e.hover = lsp::hoverText(client->request("textDocument/hover", Json{{"textDocument", Json{{"uri", lsp::pathToUri(e.symbol.path)}}}, {"position", Json{{"line", e.symbol.line}, {"character", e.symbol.character}}}}, 8000, &error));
      bool ok = false;
      const std::string text = core::readFile(e.symbol.path, &ok);
      if (ok) e.code = lsp::snippet(text, e.symbol.startLine, e.symbol.endLine, 40);
      entries.push_back(e);
      found = true;
      break;
    }
    notes.push_back(label + (found ? " found" : " not found"));
  }
  const std::string block = lsp::formatBlock(entries, folder);
  if (log) {
    std::string servers;
    for (const auto& [name, list] : files) servers += (servers.empty() ? "" : ", ") + name + " (" + std::to_string(list.size()) + " files)";
    *log = std::to_string(entries.size()) + (entries.size() == 1 ? " symbol" : " symbols") + " in " + std::to_string(elapsed()) + " ms via " + (servers.empty() ? "no server" : servers) + " - ";
    for (size_t i = 0; i < notes.size(); ++i) *log += (i ? "; " : "") + notes[i];
  }
  return block;
}

void App::addLspContext(Json* payload, const std::string& question, const std::string& folder) {
  if (!prefs().enabled || folder.empty() || question.empty() || !payload) return;
  std::string log;
  const std::string block = lspLookup(question, folder, &log);
  core::debugLog("lsp: %s", log.c_str());
  {
    std::lock_guard<std::mutex> lock(gNoteMutex);
    gLastNote = log;
  }
  if (block.empty()) return;
  Json& messages = (*payload)["messages"];
  if (messages.is_array()) messages.insert(messages.begin(), Json{{"role", "system"}, {"content", block}});
}

std::string App::lspProjectFolder() const {
  if (!projectFolder.empty()) return projectFolder;
  const Json& extra = core::Settings::instance().extra;
  return extra.contains("projectFolder") && extra["projectFolder"].is_string() ? extra["projectFolder"].get<std::string>() : std::string();
}

void App::stopLspServers() {
  std::lock_guard<std::mutex> lock(gSessionsMutex);
  gSessions.clear();
}

void App::drawLspSection(float width) {
  const auto& palette = theme::palette();
  Prefs p = prefs();
  bool changed = false;
  ImGui::PushTextWrapPos(width - 20.0f);
  ImGui::TextUnformatted("Before a message goes out, identifiers it names (`backticked`, a::b, snake_case, camelCase, f()) are looked up with the project folder's language servers, and their signatures, doc comments and definitions ride along as context. Needs a project folder (the composer's Project button).");
  ImGui::PopTextWrapPos();
  changed |= ImGui::Checkbox("Add language-server context to messages", &p.enabled);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  changed |= ImGui::SliderInt("Symbols at most", &p.maxSymbols, 1, 8);
  if (changed) {
    settings().extra["lspContext"] = Json{{"enabled", p.enabled}, {"maxSymbols", p.maxSymbols}};
    settings().save();
  }
  const std::string folder = lspProjectFolder();
  widgets::dimText("Project folder: %s", folder.empty() ? "(none - set one with the composer's Project button)" : folder.c_str());
  if (ImGui::BeginTable("##lspservers", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 20.0f, 0))) {
    ImGui::TableSetupColumn("Server", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthFixed, 260.0f);
    ImGui::TableSetupColumn("Installed");
    for (const lsp::ServerSpec& s : lsp::defaultServers()) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(s.name.c_str());
      ImGui::TableSetColumnIndex(1);
      std::string exts;
      for (size_t i = 0; i < s.extensions.size() && i < 6; ++i) exts += (i ? " " : "") + s.extensions[i];
      widgets::dimText("%s", exts.c_str());
      ImGui::TableSetColumnIndex(2);
      const std::string path = lsp::findExecutable(s.command);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(path.empty() ? palette.textDim : palette.success));
      ImGui::TextUnformatted(path.empty() ? (s.command + " not found").c_str() : path.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
  ImGui::SetNextItemWidth(width - 260.0f);
  ImGui::InputTextWithHint("##lsptry", "Try a question, e.g. How does `pool::order` rank members?", &gTryQuestion);
  ImGui::SameLine();
  widgets::beginDisabled(gTrying || folder.empty() || core::trim(gTryQuestion).empty());
  if (ImGui::Button(gTrying ? "Looking up..." : "Look up")) {
    gTrying = true;
    gTryResult.clear();
    gTryLog = "starting the language server and reading symbols...";
    const std::string q = gTryQuestion;
    async::run([this, q, folder] {
      std::string log;
      const std::string block = lspLookup(q, folder, &log);
      async::post([block, log] {
        gTrying = false;
        gTryResult = block.empty() ? "(nothing found)" : block;
        gTryLog = log;
      });
    });
  }
  widgets::endDisabled(gTrying || folder.empty() || core::trim(gTryQuestion).empty());
  ImGui::SameLine();
  if (ImGui::SmallButton("Stop servers")) {
    async::run([this] { stopLspServers(); });
    widgets::toastInfo("Language servers stopped; they start again on the next lookup");
  }
  if (!gTryLog.empty()) widgets::dimText("%s", gTryLog.c_str());
  if (!gTryResult.empty()) {
    theme::pushMono();
    ImGui::InputTextMultiline("##lspresult", &gTryResult, ImVec2(width - 20.0f, 320.0f), ImGuiInputTextFlags_ReadOnly);
    theme::pop();
  }
  std::string last;
  {
    std::lock_guard<std::mutex> lock(gNoteMutex);
    last = gLastNote;
  }
  if (!last.empty()) widgets::dimText("Last message: %s", last.c_str());
}

}  // namespace app
