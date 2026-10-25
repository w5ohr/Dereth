// Chat-workflow features and native hooks: command palette, quick ask,
// presets, pins, compare view, continue/retry, context meter and inspector,
// compaction, scheduler, in-chat search, voice mode, keyboard navigation,
// profiles, offline queue, auto-routing, Handoff/Spotlight, export.
#include <algorithm>
#include <ctime>

#include "app.h"

#include "plugins/plugin_host.h"
#include "core/async.h"
#include "core/common.h"
#include "core/store.h"
#include "core/keychain.h"
#include "export.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr int kOptionSpaceKeyCode = 49;   // kVK_Space

int64_t daysSinceEpoch() { return core::nowSeconds() / 86400; }

// Word-level diff between two strings for the compare view: returns tokens
// tagged ' ' (shared), '-' (only left) and '+' (only right).
std::vector<std::pair<char, std::string>> wordDiff(const std::string& a, const std::string& b) {
  auto words = [](const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
  };
  std::vector<std::string> x = words(a), y = words(b);
  const size_t n = std::min<size_t>(x.size(), 600), m = std::min<size_t>(y.size(), 600);
  std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
  for (size_t i = n; i-- > 0;)
    for (size_t j = m; j-- > 0;)
      lcs[i][j] = x[i] == y[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
  std::vector<std::pair<char, std::string>> out;
  size_t i = 0, j = 0;
  while (i < n && j < m) {
    if (x[i] == y[j]) { out.push_back({' ', x[i]}); ++i; ++j; }
    else if (lcs[i + 1][j] >= lcs[i][j + 1]) { out.push_back({'-', x[i]}); ++i; }
    else { out.push_back({'+', y[j]}); ++j; }
  }
  while (i < n) out.push_back({'-', x[i++]});
  while (j < m) out.push_back({'+', y[j++]});
  return out;
}

void drawWordDiffColumn(const std::vector<std::pair<char, std::string>>& tokens, char keep,
                        float width) {
  const auto& palette = theme::palette();
  float x = 0.0f;
  ImGui::BeginGroup();
  bool first = true;
  for (const auto& [tag, word] : tokens) {
    if (tag != ' ' && tag != keep) continue;
    const float w = ImGui::CalcTextSize(word.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
    if (!first && x + w < width) ImGui::SameLine();
    else x = 0.0f;
    first = false;
    if (tag != ' ') {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(pos.x - 2, pos.y), ImVec2(pos.x + w - 4, pos.y + ImGui::GetTextLineHeight()),
          keep == '+' ? IM_COL32(60, 140, 80, 110) : IM_COL32(170, 70, 70, 110), 3.0f);
    }
    ImGui::TextUnformatted(word.c_str());
    x += w;
  }
  ImGui::EndGroup();
  (void)palette;
}

std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else if (s[i] == '+') {
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}

// Floating overlays (palette, quick ask) sit over a dimmed backdrop with a
// lighter panel and a hairline border so they read as above the window.
void beginOverlayStyle() {
  const auto& palette = theme::palette();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::toVec4(palette.panel));
  ImGui::PushStyleColor(ImGuiCol_Border, theme::toVec4(palette.border));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
}

void endOverlayStyle() {
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

// Darkens everything behind an overlay. Drawn into the root window's list so
// the overlay itself (a separate window) stays on top.
void dimBackdrop() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGuiWindow* root = ImGui::FindWindowByName("##root");
  if (!root) return;
  root->DrawList->AddRectFilled(vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
                                IM_COL32(0, 0, 0, theme::isDark() ? 110 : 60));
}

}  // namespace

// ------------------------------------------------------------- presets etc.

Json Preset::toJson() const {
  return Json{{"name", name}, {"models", models}, {"params", params}, {"tools", tools},
              {"webSearch", webSearch}, {"imageGeneration", imageGeneration},
              {"codeInterpreter", codeInterpreter}};
}

Preset Preset::fromJson(const Json& j) {
  Preset p;
  p.name = j.value("name", "Preset");
  if (j.contains("models")) p.models = j["models"].get<std::vector<std::string>>();
  if (j.contains("params") && j["params"].is_object()) p.params = j["params"];
  if (j.contains("tools")) p.tools = j["tools"].get<std::vector<std::string>>();
  p.webSearch = j.value("webSearch", false);
  p.imageGeneration = j.value("imageGeneration", false);
  p.codeInterpreter = j.value("codeInterpreter", false);
  return p;
}

Json ScheduledPrompt::toJson() const {
  return Json{{"id", id}, {"name", name}, {"prompt", prompt}, {"model", model},
              {"hour", hour}, {"minute", minute}, {"weekdaysOnly", weekdaysOnly},
              {"enabled", enabled}, {"lastRunDay", lastRunDay}};
}

ScheduledPrompt ScheduledPrompt::fromJson(const Json& j) {
  ScheduledPrompt s;
  s.id = j.value("id", core::uuid());
  s.name = j.value("name", "");
  s.prompt = j.value("prompt", "");
  s.model = j.value("model", "");
  s.hour = j.value("hour", 8);
  s.minute = j.value("minute", 0);
  s.weekdaysOnly = j.value("weekdaysOnly", true);
  s.enabled = j.value("enabled", true);
  s.lastRunDay = j.value("lastRunDay", static_cast<int64_t>(0));
  return s;
}

// ------------------------------------------------------------- native hooks

void App::installNativeHooks() {
  native::Callbacks callbacks;
  callbacks.quickAsk = [this] { openQuickAsk(); };
  callbacks.captureScreen = [this] { captureScreenToChat(true); };
  callbacks.newChat = [this] { newChat(); };
  callbacks.showWindow = [] {};
  callbacks.openChat = [this](const std::string& id) {
    async::post([this, id] { openChat(id); });
  };
  callbacks.askText = [this](const std::string& text) {
    async::post([this, text] {
      lastSelectionText = text;   // what {{selection}} expands to
      openQuickAsk(text);
    });
  };
  callbacks.dropFiles = [this](const std::vector<std::string>& paths) {
    async::post([this, paths] {
      pane = Pane::Chat;
      attachFiles(paths);
      widgets::toast(widgets::ToastLevel::Success,
                     std::to_string(paths.size()) + " file(s) attached");
    });
  };
  callbacks.openUrl = [this](const std::string& url) {
    async::post([this, url] { handleOpenUrl(url); });
  };
  callbacks.notificationReply = [this](const std::string& reply) {
    async::post([this, reply] {
      pane = Pane::Chat;
      sendMessage(reply);
    });
  };
  callbacks.appearanceChanged = [this](bool) {
    async::post([this] { applyTheme(); });
  };
  callbacks.recentChats = [this] {
    std::vector<std::pair<std::string, std::string>> recent;
    for (const ChatSummary& summary : chats) {
      if (recent.size() >= 8) break;
      recent.emplace_back(summary.id, summary.title);
    }
    return recent;
  };
  native::install(callbacks);

  const Json& extra = settings().extra;
  // Secondary windows leave the global hotkeys and the menu bar extra to the
  // first instance, or every window would answer them.
  if (secondaryInstance) {
    native::enableDropOnMainWindow();
    applyWindowMode();
    return;
  }
  if (extra.value("captureHotkey", true)) {
    // Control-Option-S from any app: capture a region or window into the chat.
    if (!native::registerCaptureHotkey(1, false, true, true, false)) {
      widgets::toastInfo("Control-Option-S is taken by another app; the capture hotkey is off");
    }
  }
  if (extra.value("globalHotkey", true)) {
    if (!native::registerHotkey(kOptionSpaceKeyCode, false, true, false, false)) {
      widgets::toastInfo("Option-Space is taken by another app; quick ask hotkey not registered");
    }
  }
  native::setStatusItemVisible(extra.value("menuBarExtra", true));
  native::enableDropOnMainWindow();
}

void App::handleOpenUrl(const std::string& url) {
  // owui://ask?prompt=...&model=...   owui://chat/<id>   owui://new
  std::string rest = url;
  if (core::startsWith(rest, "owui://")) rest = rest.substr(7);
  std::string path = rest, query;
  size_t q = rest.find('?');
  if (q != std::string::npos) {
    path = rest.substr(0, q);
    query = rest.substr(q + 1);
  }
  std::map<std::string, std::string> params;
  for (const std::string& pair : core::split(query, '&')) {
    size_t eq = pair.find('=');
    if (eq == std::string::npos) continue;
    params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
  }
  while (!path.empty() && path.back() == '/') path.pop_back();

  if (path == "ask" || path == "new") {
    if (params.count("model")) selectedModels = {params["model"]};
    newChat();
    if (params.count("prompt") && !params["prompt"].empty()) {
      const bool noSend = params.count("send") && params["send"] == "0";
      if (noSend) {
        composer = params["prompt"];
      } else {
        sendMessage(params["prompt"]);
      }
    }
  } else if (core::startsWith(path, "chat/")) {
    openChat(path.substr(5));
  } else if (path == "quick") {
    openQuickAsk(params["prompt"]);
  } else {
    widgets::toastInfo("Unknown link: " + url);
  }
}

void App::updateHandoffAndSpotlight() {
  if (chat.id.empty() || chat.isPrivate) {
    native::clearHandoff();
    return;
  }
  native::updateHandoff(api_.server() + "/c/" + chat.id, chat.title);
  std::string preview;
  for (const std::string& id : chat.branch()) {
    const Message& message = chat.messages.at(id);
    if (message.role != "user") continue;
    preview = message.content.substr(0, 200);
    break;
  }
  native::indexChatForSpotlight(chat.id, chat.title, preview);
}

void App::applyTheme() {
  std::string name = settings().theme;
  if (name == "system") name = native::systemIsDark() ? "dark" : "light";
  theme::apply(name);
}

// ----------------------------------------------------------------- palette

void App::drawPalette() {
  if (!showPalette) return;
  struct Entry {
    std::string label;
    std::string hint;
    std::function<void()> run;
  };
  std::vector<Entry> entries;
  const std::string q = core::lower(paletteQuery);
  auto matches = [&](const std::string& text) {
    return q.empty() || core::lower(text).find(q) != std::string::npos;
  };

  auto add = [&](const std::string& label, const std::string& hint, std::function<void()> run) {
    if (matches(label) || matches(hint)) entries.push_back({label, hint, std::move(run)});
  };

  add("New chat", "command", [this] { newChat(); });
  add("Quick ask", "command", [this] { openQuickAsk(); });
  add("Capture screen region to chat", "command", [this] { captureScreenToChat(true); });
  add("Open this chat in a new window", "command", [this] { openChatInNewWindow(chat.id, false); });
  add("Open this chat as a floating mini window", "command", [this] { openChatInNewWindow(chat.id, true); });
  add("Float this window on top", "command", [this] { toggleFloating(); });
  add("Toggle conversation tree", "command", [this] { showTree = !showTree; });
  add("New private chat", "command", [this] { newPrivateChat(); });
  add("Undo the last apply to files", "command", [this] { undoLastApply(); });
  add("Compare models", "command", [this] { openModelCompare(); });
  add("Usage dashboard (Tools > Usage)", "command", [this] { pane = Pane::Tools; });
  add("Export everything to a backup zip", "command", [this] { exportEverything(); });
  add("Run the project's tests until green", "command", [this] { pane = Pane::Tools; testLoopRun(true); });
  add("Audit log (what left this Mac)", "command", [this] { pane = Pane::Tools; });
  for (const recipes::Recipe& r : recipeList()) {
    const std::string id = r.id;
    add("Run recipe: " + r.name, "recipe - " + std::to_string(r.steps.size()) + " steps", [this, id] { runRecipe(id); });
  }
  if (recipeRunning()) add("Stop the running recipe", "command", [this] { stopRecipe(); });
  add("Import chats (ChatGPT, Claude, backup)", "command", [this] { importChats(); });
  add("Toggle agent mode", "command", [this] {
    agentMode = !agentMode;
    settings().extra["agentMode"] = agentMode;
    settings().save();
  });
  for (const plugins::Command& command : plugins::Host::instance().commands()) {
    const std::string pluginId = command.pluginId;
    const std::string name = command.name;
    add(command.name, "plugin: " + command.pluginName + (command.description.empty() ? "" : " - " + command.description),
        [this, pluginId, name] { plugins::Host::instance().runCommand(pluginId, name, pluginSnapshot()); });
  }
  add("Capture whole screen to chat", "command", [this] { captureScreenToChat(false); });
  add("Export chat as Markdown", "command", [this] { exportChat("md"); });
  add("Export chat as HTML", "command", [this] { exportChat("html"); });
  add("Export chat as PDF", "command", [this] { exportChat("pdf"); });
  add("Compact this chat", "command", [this] { compactChat(); });
  add(compareMode ? "Hide compare view" : "Compare model answers side by side", "command",
      [this] { compareMode = !compareMode; });
  add(showPins ? "Hide pinned rail" : "Show pinned rail", "command", [this] { showPins = !showPins; });
  add(voiceMode ? "Stop voice conversation" : "Start voice conversation", "command",
      [this] { voiceMode = !voiceMode; });
  add("Show what was sent (context inspector)", "command", [this] { showContextInspector = true; });
  add("Find in chat", "command", [this] { showChatSearch = true; });
  add("Attach folder as context", "command", [this] {
    auto picked = native::chooseFolder();
    if (!picked.empty()) attachFolderAsContext(picked.front());
  });
  add("Set project folder", "command", [this] {
    auto picked = native::chooseFolder();
    if (!picked.empty()) setProjectFolder(picked.front());
  });
  add("Attach git diff", "command", [this] {
    if (!projectFolder.empty()) attachGitContext(projectFolder, "diff");
    else widgets::toastInfo("Set a project folder first");
  });
  if (!attachments.empty() && attachments.back().isImage) {
    add("Extract text from attached image (Live Text)", "command", [this] {
      const std::string bytes = attachments.back().bytes;
      async::run([this, bytes] {
        const std::string text = native::recognizeText(bytes);
        async::post([this, text] {
          if (text.empty()) {
            widgets::toastInfo("No text found in the image");
            return;
          }
          composer += (composer.empty() ? "" : "\n") + text;
          ++composerGeneration;
          focusComposer = true;
        });
      });
    });
  }
  add("Toggle sidebar", "command", [this] { sidebarVisible = !sidebarVisible; });
  add("Stop generating", "command", [this] { stopAllStreams(); });

  const std::pair<const char*, Pane> panes[] = {
      {"Go to Chats", Pane::Chat}, {"Go to Notes", Pane::Notes}, {"Go to Channels", Pane::Channels},
      {"Go to Workspace", Pane::Workspace}, {"Go to Images", Pane::Images}, {"Go to Files", Pane::Files},
      {"Go to Memories", Pane::Memories}, {"Go to Admin", Pane::Admin}, {"Go to API Console", Pane::Console},
      {"Go to Tools", Pane::Tools}, {"Go to Settings", Pane::Settings}};
  for (const auto& [label, target] : panes) {
    Pane p = target;
    add(label, "pane", [this, p] { pane = p; });
  }
  for (const ModelInfo& info : models) {
    std::string id = info.id;
    add("Use model: " + info.name, "model", [this, id] {
      selectedModels = {id};
      settings().selectedModels = selectedModels;
    });
  }
  for (const auto& prompt : promptList) {
    std::string content = prompt.value("content", "");
    add("Prompt: " + prompt.value("title", prompt.value("command", "")), "prompt", [this, content] {
      composer = content;
      pane = Pane::Chat;
      focusComposer = true;
    });
  }
  for (const ChatSummary& summary : chats) {
    std::string id = summary.id;
    add(summary.title, "chat", [this, id] { openChat(id); });
  }
  for (const auto& presetJson : settings().extra.value("presets", Json::array())) {
    Preset preset = Preset::fromJson(presetJson);
    add("Preset: " + preset.name, "preset", [this, preset] { applyPreset(preset); });
  }

  static int selected = 0;
  if (entries.empty()) selected = 0;
  selected = std::clamp(selected, 0, std::max(0, static_cast<int>(entries.size()) - 1));

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 90.0f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(620, 0));
  dimBackdrop();
  beginOverlayStyle();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
  ImGui::Begin("##palette", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::InputTextWithHint("##pq", "Type a command, chat, model or prompt...", &paletteQuery);
  const bool inputActive = ImGui::IsItemActive();
  ImGui::Dummy(ImVec2(0, 4));

  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) selected = std::min(selected + 1, static_cast<int>(entries.size()) - 1);
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) selected = std::max(selected - 1, 0);

  ImGui::BeginChild("##plist", ImVec2(0, std::min(420.0f, entries.size() * 30.0f + 8.0f)));
  for (int i = 0; i < static_cast<int>(entries.size()) && i < 200; ++i) {
    const Entry& entry = entries[i];
    ImGui::PushID(i);
    const bool isSelected = i == selected;
    if (ImGui::Selectable(entry.label.c_str(), isSelected, 0, ImVec2(0, 24))) {
      entry.run();
      showPalette = false;
      paletteQuery.clear();
    }
    if (isSelected && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
      ImGui::SetScrollHereY();
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 70.0f);
    widgets::dimText("%s", entry.hint.c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();

  if ((inputActive || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) &&
      ImGui::IsKeyPressed(ImGuiKey_Enter) && !entries.empty()) {
    entries[selected].run();
    showPalette = false;
    paletteQuery.clear();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    showPalette = false;
    paletteQuery.clear();
  }
  ImGui::End();
  ImGui::PopStyleVar();
  endOverlayStyle();
}

// -------------------------------------------------------------- quick ask

void App::openQuickAsk(const std::string& prefill) {
  showQuickAsk = true;
  quickAskInput = prefill;
  quickAskChatId.clear();
}

void App::drawQuickAsk() {
  if (!showQuickAsk) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 120.0f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(700, 0));
  dimBackdrop();
  beginOverlayStyle();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::Begin("##quickask", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
  theme::pushBold();
  ImGui::TextUnformatted("Quick ask");
  theme::pop();
  ImGui::SameLine();
  widgets::dimText("%s", selectedModels.empty() ? "" : selectedModels.front().c_str());
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  const bool submitted = ImGui::InputTextWithHint("##qa", "Ask anything, Enter to send, Esc to close",
                                                  &quickAskInput, ImGuiInputTextFlags_EnterReturnsTrue);
  if (submitted && !core::trim(quickAskInput).empty()) {
    newChat();
    sendMessage(core::trim(quickAskInput));
    quickAskChatId = "pending";
    quickAskInput.clear();
  }

  // Show the answer as it streams: it is the last assistant message of the
  // chat that quick ask just created.
  if (!quickAskChatId.empty()) {
    const std::vector<std::string> path = chat.branch();
    const Message* answer = nullptr;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      const Message& m = chat.messages.at(*it);
      if (m.role == "assistant") { answer = &m; break; }
    }
    ImGui::Separator();
    ImGui::BeginChild("##qaanswer", ImVec2(0, 320));
    if (answer) {
      if (answer->streaming && answer->content.empty()) {
        widgets::loadingRow("Thinking...");
      } else {
        md::Options options;
        options.wrapWidth = ImGui::GetContentRegionAvail().x - 10.0f;
        md::render("qa", answer->content, options);
      }
    }
    ImGui::EndChild();
    if (answer && !answer->streaming) {
      if (ImGui::Button("Copy")) platform::setClipboardText(answer->content);
      ImGui::SameLine();
      if (ImGui::Button("Open in chat")) {
        showQuickAsk = false;
        pane = Pane::Chat;
      }
      ImGui::SameLine();
      if (ImGui::Button("Ask again")) quickAskChatId.clear();
    }
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showQuickAsk = false;
  ImGui::End();
  ImGui::PopStyleVar();
  endOverlayStyle();
}

// ------------------------------------------------------- context inspector

int App::estimateTokens(const std::string& text) const {
  // Rough but consistent: ~4 chars per token for prose, denser for code.
  size_t codeChars = 0;
  bool inFence = false;
  for (const std::string& line : core::split(text, '\n')) {
    if (core::startsWith(core::trim(line), "```")) { inFence = !inFence; continue; }
    if (inFence) codeChars += line.size() + 1;
  }
  const size_t prose = text.size() > codeChars ? text.size() - codeChars : 0;
  return static_cast<int>(prose / 4.0 + codeChars / 3.2) + 4;
}

int App::contextWindowFor(const std::string& modelId) const {
  for (const ModelInfo& info : models) {
    if (info.id != modelId) continue;
    // Ollama exposes the trained context; the server's effective window is
    // whatever num_ctx is, which we can't see, so this is an upper bound.
    if (info.raw.contains("ollama") && info.raw["ollama"].is_object()) {
      const Json& details = info.raw["ollama"].value("details", Json::object());
      if (details.contains("context_length")) return details["context_length"].get<int>();
    }
    if (info.raw.contains("info") && info.raw["info"].is_object()) {
      const Json& params = info.raw["info"].value("params", Json::object());
      if (params.contains("num_ctx")) return params["num_ctx"].get<int>();
    }
  }
  if (chat.params.contains("num_ctx")) return chat.params["num_ctx"].get<int>();
  return 16384;
}

int App::currentContextTokens() const {
  int total = 0;
  for (const std::string& id : chat.branch()) {
    auto it = chat.messages.find(id);
    if (it != chat.messages.end()) total += estimateTokens(it->second.content);
  }
  total += estimateTokens(composer);
  if (chat.params.contains("system")) total += estimateTokens(chat.params["system"].get<std::string>());
  return total;
}

void App::drawContextInspector() {
  if (!showContextInspector) return;
  ImGui::OpenPopup("What was sent");
  ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("What was sent", &showContextInspector)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      showContextInspector = false;
      ImGui::CloseCurrentPopup();
    }
    if (lastPayload.empty()) {
      widgets::dimText("Nothing sent yet in this session.");
    } else {
      const Json& messages = lastPayload.value("messages", Json::array());
      int tokens = 0;
      for (const auto& m : messages) {
        if (m.contains("content") && m["content"].is_string()) tokens += estimateTokens(m["content"]);
      }
      ImGui::Text("%d messages, ~%d tokens, model %s", static_cast<int>(messages.size()), tokens,
                  lastPayload.value("model", "").c_str());
      if (lastPayload.contains("files")) ImGui::Text("%d file reference(s)", static_cast<int>(lastPayload["files"].size()));
      if (lastPayload.contains("tool_ids")) ImGui::Text("%d tool(s)", static_cast<int>(lastPayload["tool_ids"].size()));
      if (lastPayload.contains("features")) ImGui::Text("features: %s", lastPayload["features"].dump().c_str());
      ImGui::Separator();
      ImGui::BeginChild("##payload", ImVec2(0, -36));
      theme::pushMono();
      ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
      ImGui::TextUnformatted(lastPayload.dump(2).c_str());
      ImGui::PopTextWrapPos();
      theme::pop();
      ImGui::EndChild();
      if (ImGui::Button("Copy JSON")) platform::setClipboardText(lastPayload.dump(2));
    }
    ImGui::EndPopup();
  }
}

// -------------------------------------------------------- in-chat search

void App::drawChatSearchBar(float width) {
  if (!showChatSearch) return;
  const std::vector<std::string> path = chat.branch();
  std::vector<std::string> hits;
  for (const std::string& id : path) {
    if (!chatSearchQuery.empty() && core::containsNoCase(chat.messages.at(id).content, chatSearchQuery)) {
      hits.push_back(id);
    }
  }
  if (!hits.empty()) chatSearchIndex = std::clamp(chatSearchIndex, 0, static_cast<int>(hits.size()) - 1);

  ImGui::SetNextItemWidth(std::min(360.0f, width - 220.0f));
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  if (ImGui::InputTextWithHint("##chatfind", "Find in chat", &chatSearchQuery)) chatSearchIndex = 0;
  const bool enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
  ImGui::SameLine();
  if (ImGui::SmallButton("<") && !hits.empty()) chatSearchIndex = (chatSearchIndex + static_cast<int>(hits.size()) - 1) % hits.size();
  ImGui::SameLine();
  if ((ImGui::SmallButton(">") || enter) && !hits.empty()) chatSearchIndex = (chatSearchIndex + 1) % hits.size();
  ImGui::SameLine();
  widgets::dimText("%d / %d", hits.empty() ? 0 : chatSearchIndex + 1, static_cast<int>(hits.size()));
  ImGui::SameLine();
  if (ImGui::SmallButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) showChatSearch = false;
  if (!hits.empty()) pendingScrollTo = hits[chatSearchIndex];
}

// --------------------------------------------------------------- presets

void App::applyPreset(const Preset& preset) {
  if (!preset.models.empty()) {
    selectedModels = preset.models;
    settings().selectedModels = selectedModels;
  }
  chat.params = preset.params;
  activeToolIds = preset.tools;
  webSearchEnabled = preset.webSearch;
  imageGenerationEnabled = preset.imageGeneration;
  codeInterpreterEnabled = preset.codeInterpreter;
  widgets::toast(widgets::ToastLevel::Success, "Preset applied: " + preset.name);
}

void App::saveCurrentAsPreset(const std::string& name) {
  Preset preset;
  preset.name = name;
  preset.models = selectedModels;
  preset.params = chat.params;
  preset.tools = activeToolIds;
  preset.webSearch = webSearchEnabled;
  preset.imageGeneration = imageGenerationEnabled;
  preset.codeInterpreter = codeInterpreterEnabled;
  Json& list = settings().extra["presets"];
  if (!list.is_array()) list = Json::array();
  for (auto& existing : list) {
    if (existing.value("name", "") == name) {
      existing = preset.toJson();
      settings().save();
      return;
    }
  }
  list.push_back(preset.toJson());
  settings().save();
}

void App::drawPresetMenu() {
  static std::string newName;
  if (ImGui::BeginPopup("##presets")) {
    const Json list = settings().extra.value("presets", Json::array());
    if (list.empty()) widgets::dimText("No presets yet");
    for (const auto& item : list) {
      Preset preset = Preset::fromJson(item);
      if (ImGui::MenuItem(preset.name.c_str())) applyPreset(preset);
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete")) {
          Json& stored = settings().extra["presets"];
          Json kept = Json::array();
          for (const auto& p : stored) if (p.value("name", "") != preset.name) kept.push_back(p);
          stored = kept;
          settings().save();
        }
        ImGui::EndPopup();
      }
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputTextWithHint("##presetname", "Save current as...", &newName,
                                 ImGuiInputTextFlags_EnterReturnsTrue) &&
        !core::trim(newName).empty()) {
      saveCurrentAsPreset(core::trim(newName));
      newName.clear();
    }
    ImGui::EndPopup();
  }
}

// ------------------------------------------------ continue / retry / compact

void App::continueReply(const std::string& assistantId) {
  auto it = chat.messages.find(assistantId);
  if (it == chat.messages.end() || it->second.role != "assistant") return;
  if (!streams_.empty() || it->second.streaming) return;
  Message& message = it->second;
  if (core::trim(message.content).empty()) {
    regenerate(assistantId);
    return;
  }
  std::string model = message.model.empty() ? (selectedModels.empty() ? "" : selectedModels.front())
                                            : message.model;
  if (model.empty()) return;
  message.streaming = true;
  message.serverStream = false;   // runs over HTTP; the server would restart the message
  message.error.clear();
  message.structuredChecked = false;
  continueFromId = assistantId;
  startStream(assistantId, model);
  continueFromId.clear();
}

void App::saveEditedReply(const std::string& assistantId, const std::string& text, bool andContinue) {
  auto it = chat.messages.find(assistantId);
  if (it == chat.messages.end()) return;
  Message& message = it->second;
  message.content = text;
  message.editing = false;
  message.error.clear();
  message.structuredChecked = false;
  saveActiveChat();
  if (andContinue) continueReply(assistantId);
}

void App::retryWithModel(const std::string& assistantId, const std::string& model) {
  activeStreamModelOverride_ = model;
  regenerate(assistantId);
  activeStreamModelOverride_.clear();
}

void App::compactChat() {
  if (chat.id.empty()) return;
  const std::string id = chat.id;
  widgets::toastInfo("Compacting...");
  async::run([this, id] {
    api::Result res = api_.post("/api/v1/chats/" + id + "/compact");
    async::post([this, res, id] {
      if (!res.ok) {
        widgets::toastError("Compaction failed: " + api::errorMessage(res));
        return;
      }
      widgets::toast(widgets::ToastLevel::Success, "Chat compacted");
      chat.id.clear();   // force reload
      openChat(id);
    });
  });
}

// ------------------------------------------------------------------ pins

bool App::isPinned(const std::string& messageId) const {
  if (!chat.raw.contains("desktop_pins") || !chat.raw["desktop_pins"].is_array()) return false;
  for (const auto& id : chat.raw["desktop_pins"]) {
    if (id.is_string() && id.get<std::string>() == messageId) return true;
  }
  return false;
}

void App::togglePin(const std::string& messageId) {
  Json& pins = chat.raw["desktop_pins"];
  if (!pins.is_array()) pins = Json::array();
  Json kept = Json::array();
  bool removed = false;
  for (const auto& id : pins) {
    if (id.is_string() && id.get<std::string>() == messageId) { removed = true; continue; }
    kept.push_back(id);
  }
  if (!removed) kept.push_back(messageId);
  pins = kept;
  saveActiveChat();
}

void App::drawPinnedRail(float width) {
  ImGui::BeginChild("##pins", ImVec2(width, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(10.0f);
  widgets::sectionHeader("PINNED");
  const Json pins = chat.raw.value("desktop_pins", Json::array());
  if (pins.empty()) widgets::dimText("Pin a message from its action row.");
  for (const auto& idJson : pins) {
    if (!idJson.is_string()) continue;
    const std::string id = idJson.get<std::string>();
    auto it = chat.messages.find(id);
    if (it == chat.messages.end()) continue;
    ImGui::PushID(id.c_str());
    std::string excerpt = md::toPlainText(it->second.content).substr(0, 160);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 24.0f);
    if (ImGui::Selectable(excerpt.c_str(), false, 0, ImVec2(width - 24.0f, 0))) pendingScrollTo = id;
    ImGui::PopTextWrapPos();
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Unpin")) togglePin(id);
      if (ImGui::MenuItem("Copy")) platform::setClipboardText(it->second.content);
      ImGui::EndPopup();
    }
    ImGui::Separator();
    ImGui::PopID();
  }
  ImGui::Unindent(10.0f);
  ImGui::EndChild();
}

// --------------------------------------------------------------- scheduler

void App::tickScheduler() {
  static double lastCheck = 0.0;
  const double now = core::nowMillis();
  if (now - lastCheck < 30000.0) return;
  lastCheck = now;
  if (!signedIn_) return;

  // Look before indexing: operator[] would write a null "schedules" key into
  // the saved settings just by checking.
  Json& extra = settings().extra;
  if (!extra.contains("schedules") || !extra["schedules"].is_array() || extra["schedules"].empty()) {
    return;
  }
  Json& list = extra["schedules"];
  std::time_t t = static_cast<std::time_t>(core::nowSeconds());
  std::tm local{};
  localtime_r(&t, &local);
  const int64_t today = daysSinceEpoch();
  bool changed = false;
  for (auto& item : list) {
    ScheduledPrompt s = ScheduledPrompt::fromJson(item);
    if (!s.enabled || s.lastRunDay == today) continue;
    if (s.weekdaysOnly && (local.tm_wday == 0 || local.tm_wday == 6)) continue;
    if (local.tm_hour < s.hour || (local.tm_hour == s.hour && local.tm_min < s.minute)) continue;
    // Fire: a fresh chat with the prompt, so the output is kept and titled;
    // a "recipe:<id>" prompt runs that recipe instead.
    if (core::startsWith(s.prompt, "recipe:")) {
      if (recipeRunning()) continue;
      runRecipe(s.prompt.substr(7));
    } else {
      if (!s.model.empty()) selectedModels = {s.model};
      newChat();
      chat.title = s.name.empty() ? "Scheduled: " + s.prompt.substr(0, 40) : s.name;
      sendMessage(s.prompt);
    }
    s.lastRunDay = today;
    item = s.toJson();
    changed = true;
    widgets::toastInfo("Scheduled prompt ran: " + chat.title);
    break;   // one per tick keeps the UI sane
  }
  if (changed) settings().save();
}

// --------------------------------------------------------------- voice

void App::tickVoiceMode() {
  if (!voiceMode) {
    if (voiceState != 0 && platform::recording()) platform::stopRecording();
    voiceState = 0;
    return;
  }
  const double now = core::nowMillis();
  switch (voiceState) {
    case 0:   // idle -> listen
      if (platform::startRecording()) {
        voiceState = 1;
        voiceHeardSpeech = false;
        voiceSilenceSince = now;
      } else {
        widgets::toastError("Microphone unavailable");
        voiceMode = false;
      }
      break;
    case 1: {  // listening: stop after ~1.4 s of silence following speech
      const float level = platform::recordingLevel();
      if (level > 0.35f) {
        voiceHeardSpeech = true;
        voiceSilenceSince = now;
      }
      const bool longEnough = voiceHeardSpeech && now - voiceSilenceSince > 1400.0;
      const bool tooLong = now - voiceSilenceSince > 20000.0;
      if (longEnough || tooLong) {
        const std::string wav = platform::stopRecording();
        if (!voiceHeardSpeech || wav.empty()) {
          voiceState = 0;
          break;
        }
        voiceState = 2;
        async::run([this, wav] {
          api::Result res = api_.transcribe("voice.wav", "audio/wav", wav);
          async::post([this, res] {
            std::string text = res.ok ? core::trim(res.data.value("text", "")) : "";
            if (text.empty()) {
              voiceState = 0;
              return;
            }
            sendMessage(text);
            voiceState = 3;
          });
        });
      }
      break;
    }
    case 2:   // transcribing
      break;
    case 3: {  // waiting for the reply to finish, then speak it
      if (!streams_.empty()) break;
      const std::vector<std::string> path = chat.branch();
      for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const Message& m = chat.messages.at(*it);
        if (m.role == "assistant") {
          if (!m.content.empty()) speak(m.content);
          break;
        }
      }
      voiceState = 4;
      voiceSilenceSince = now;
      break;
    }
    case 4:   // speaking: resume listening once playback ends
      if (!platform::audioPlaying() && now - voiceSilenceSince > 800.0) voiceState = 0;
      break;
  }
}

// --------------------------------------------------------- offline queue

void App::tickOfflineQueue() {
  const double now = core::nowMillis();
  if (serverReachable && offlineQueue.empty()) return;
  if (now - lastReachCheck < 15000.0) return;
  lastReachCheck = now;
  async::run([this] {
    api::Result res = api_.config();
    async::post([this, res] {
      const bool wasDown = !serverReachable;
      serverReachable = res.ok;
      if (serverReachable && wasDown) {
        widgets::toast(widgets::ToastLevel::Success, "Server is back");
        syncMirror(true);
        refreshChats();
      }
      if (serverReachable && !offlineQueue.empty()) {
        std::vector<std::string> queued;
        queued.swap(offlineQueue);
        for (const std::string& text : queued) sendMessage(text);
        widgets::toastInfo(std::to_string(queued.size()) + " queued message(s) sent");
      }
    });
  });
}

// ------------------------------------------------------ keyboard nav

void App::handleKeyboardNavigation(const std::vector<std::string>& path) {
  ImGuiIO& io = ImGui::GetIO();
  if (path.empty()) return;

  // Escape hands the keyboard back to the composer.
  if (!focusedMessageId.empty() && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    focusedMessageId.clear();
    focusComposer = true;
    return;
  }

  auto position = std::find(path.begin(), path.end(), focusedMessageId);
  int index = position == path.end() ? static_cast<int>(path.size()) : static_cast<int>(std::distance(path.begin(), position));

  // Option+Up/Down walks the transcript. It also takes focus out of the
  // composer: on macOS Option+letter types a character (R gives a registered
  // sign), so the letter shortcuts below only fire with no text field active.
  if (io.KeyAlt && (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ||
                    ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))) {
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) index = std::max(0, index - 1);
    else index = std::min(static_cast<int>(path.size()) - 1, index + 1);
    ImGui::ClearActiveID();
    focusedMessageId = path[index];
    pendingScrollTo = focusedMessageId;
    return;
  }

  if (position == path.end() || io.WantTextInput || native::composerIsFocused() || io.KeySuper ||
      io.KeyCtrl) {
    return;
  }
  const Message& m = chat.messages.at(focusedMessageId);
  // Plain letters act on the focused message: c copy, r regenerate, p pin,
  // e edit, s speak. Arrow keys without Option move too, once out of the
  // composer.
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
    focusedMessageId = path[std::max(0, index - 1)];
    pendingScrollTo = focusedMessageId;
  } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
    focusedMessageId = path[std::min(static_cast<int>(path.size()) - 1, index + 1)];
    pendingScrollTo = focusedMessageId;
  } else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
    platform::setClipboardText(m.content);
    widgets::toast(widgets::ToastLevel::Success, "Copied");
  } else if (ImGui::IsKeyPressed(ImGuiKey_R, false) && m.role == "assistant") {
    regenerate(focusedMessageId);
  } else if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
    togglePin(focusedMessageId);
  } else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && m.role == "assistant") {
    speak(m.content);
  } else if (ImGui::IsKeyPressed(ImGuiKey_E, false) && m.role == "user") {
    chat.messages[focusedMessageId].editing = true;
    chat.messages[focusedMessageId].editBuffer = m.content;
  }
}

// --------------------------------------------------------------- profiles

std::vector<ServerProfile> App::profiles() const {
  std::vector<ServerProfile> out;
  const core::Settings& cfg = core::Settings::instance();
  const Json list = cfg.extra.value("profiles", Json::array());
  for (const auto& item : list) {
    out.push_back({item.value("name", ""), item.value("url", ""), item.value("email", "")});
  }
  if (out.empty()) out.push_back({"Default", cfg.serverUrl, cfg.email});
  return out;
}

void App::saveProfiles(const std::vector<ServerProfile>& list) {
  Json stored = Json::array();
  for (const ServerProfile& p : list) stored.push_back(Json{{"name", p.name}, {"url", p.url}, {"email", p.email}});
  settings().extra["profiles"] = stored;
  settings().save();
}

void App::switchProfile(const ServerProfile& profile) {
  stopAllStreams();
  socket_.stop();
  signedIn_ = false;
  user_ = Json::object();
  chats.clear();
  chat = ChatDoc();
  models.clear();
  settings().serverUrl = profile.url;
  settings().email = profile.email;
  settings().save();
  api_.setServer(profile.url);
  serverInput_ = profile.url;
  store::open(profile.url);
  loginEmail_ = profile.email;
  api_.setToken("");
  // Tokens are stored per server so switching never mixes sessions.
  const std::string token = keychain::load("session-token:" + api_.server());
  if (!token.empty()) {
    api_.setToken(token);
    loadSession();
  }
  async::run([this] {
    api::Result res = api_.config();
    async::post([this, res] { if (res.ok) serverConfig_ = res.data; });
  });
}

// ----------------------------------------------------------- auto-routing

std::string App::routeModel(const std::string& text) const {
  const Json& extra = core::Settings::instance().extra;
  if (!extra.value("autoRoute", false)) return "";
  const std::string small = extra.value("autoRouteSmallModel", "");
  if (small.empty()) return "";
  const bool hasCode = text.find("```") != std::string::npos;
  const bool longPrompt = estimateTokens(text) > extra.value("autoRouteTokenThreshold", 80);
  const bool heavyWords = core::containsNoCase(text, "refactor") || core::containsNoCase(text, "implement") ||
                          core::containsNoCase(text, "debug") || core::containsNoCase(text, "analy");
  if (!hasCode && !longPrompt && !heavyWords && attachments.empty() && chat.branch().size() < 6) return small;
  return "";
}

// ----------------------------------------------------------- slash prompts

Json App::slashPromptMatches(const std::string& prefix) const {
  Json out = Json::array();
  const std::string needle = core::lower(prefix);
  for (const auto& prompt : promptList) {
    std::string command = core::lower(prompt.value("command", ""));
    if (command.empty()) continue;
    if (!command.empty() && command[0] != '/') command = "/" + command;
    if (core::startsWith(command, needle)) out.push_back(prompt);
    if (out.size() >= 8) break;
  }
  return out;
}

// --------------------------------------------------------------- compare

void App::drawCompareView(const std::vector<std::string>& siblings, float width) {
  if (siblings.size() < 2) return;
  const std::string& leftId = siblings[0];
  const std::string& rightId = siblings[1];
  const Message& left = chat.messages.at(leftId);
  const Message& right = chat.messages.at(rightId);
  const auto tokens = wordDiff(left.content, right.content);
  const float column = (width - 24.0f) * 0.5f;

  ImGui::BeginChild("##cmpL", ImVec2(column, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
  theme::pushBold();
  ImGui::TextUnformatted(left.modelName.c_str());
  theme::pop();
  drawWordDiffColumn(tokens, '-', column - 20.0f);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##cmpR", ImVec2(column, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
  theme::pushBold();
  ImGui::TextUnformatted(right.modelName.c_str());
  theme::pop();
  drawWordDiffColumn(tokens, '+', column - 20.0f);
  ImGui::EndChild();
}

// --------------------------------------------------------- reference column

void App::drawReferenceColumn(float width) {
  ImGui::BeginChild("##reference", ImVec2(width, 0), ImGuiChildFlags_Borders);
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::Indent(10.0f);
  theme::pushBold();
  ImGui::TextUnformatted(referenceChat.title.c_str());
  theme::pop();
  ImGui::SameLine(width - 60.0f);
  if (ImGui::SmallButton("Close")) {
    referenceChatId.clear();
    referenceChat = ChatDoc();
  }
  ImGui::Separator();
  for (const std::string& id : referenceChat.branch()) {
    const Message& m = referenceChat.messages.at(id);
    widgets::dimText("%s", m.role == "user" ? "You" : m.modelName.c_str());
    md::Options options;
    options.wrapWidth = width - 30.0f;
    options.compact = true;
    md::render(id.c_str(), m.content, options);
    ImGui::Dummy(ImVec2(0, 8));
  }
  ImGui::Unindent(10.0f);
  ImGui::EndChild();
}

// ---------------------------------------------------------------- export

void App::exportChat(const std::string& format) {
  if (chat.messages.empty()) {
    widgets::toastInfo("Nothing to export");
    return;
  }
  const std::string ext = format == "md" ? "md" : (format == "pdf" ? "pdf" : "html");
  const std::string path = platform::saveFile(chat.title + "." + ext);
  if (path.empty()) return;
  bool ok = false;
  if (format == "md") {
    ok = core::writeFile(path, exporter::toMarkdown(chat));
  } else if (format == "html") {
    ok = core::writeFile(path, exporter::toHtml(chat, theme::isDark()));
  } else {
    ok = exporter::htmlToPdf(exporter::toHtml(chat, false), path);
  }
  if (ok) {
    widgets::toast(widgets::ToastLevel::Success, "Exported to " + path);
    platform::revealInFinder(path);
  } else {
    widgets::toastError("Export failed");
  }
}

}  // namespace app
