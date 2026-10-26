// The artifact pane: HTML, SVG and Mermaid blocks in replies rendered live in
// a WKWebView beside the chat, with an editor, console output, save and
// open-in-browser. Settings: extra["artifactAutoOpen"] (default on).
#include "app.h"
#include "core/async.h"
#include "core/i18n.h"

#include <algorithm>
#include <functional>

#include "core/common.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr float kArtifactColumnWidth = 540.0f;

std::string normaliseLanguage(const std::string& language) {
  std::string lang = core::lower(core::trim(language));
  if (lang == "htm" || lang == "xhtml") lang = "html";
  if (lang == "mmd") lang = "mermaid";
  return lang;
}

// Walks the fenced code blocks of a markdown body.
void forEachFence(const std::string& content,
                  const std::function<void(const std::string& language, const std::string& code)>& fn) {
  std::string language;
  std::string code;
  bool inFence = false;
  size_t pos = 0;
  while (pos <= content.size()) {
    const size_t end = content.find('\n', pos);
    const std::string line = content.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    const std::string trimmed = core::trim(line);
    if (!inFence) {
      if (core::startsWith(trimmed, "```")) {
        inFence = true;
        language = core::trim(trimmed.substr(3));
        code.clear();
      }
    } else if (core::startsWith(trimmed, "```")) {
      inFence = false;
      fn(language, code);
    } else {
      code += line + "\n";
    }
    if (end == std::string::npos) break;
    pos = end + 1;
  }
}

std::string titleFor(const std::string& language, const std::string& code, int ordinal) {
  if (language == "html") {
    for (const char* tag : {"<title>", "<h1>"}) {
      const size_t open = core::lower(code).find(tag);
      if (open == std::string::npos) continue;
      const size_t start = open + std::strlen(tag);
      const size_t close = code.find('<', start);
      const std::string title = core::trim(code.substr(start, close == std::string::npos ? 60 : close - start));
      if (!title.empty()) return title.substr(0, 60);
    }
  }
  return language + " #" + std::to_string(ordinal);
}

std::string escapeHtml(const std::string& text) {
  std::string out;
  for (char ch : text) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += ch;
    }
  }
  return out;
}

}  // namespace

bool App::isArtifactLanguage(const std::string& language, const std::string& code) {
  const std::string lang = normaliseLanguage(language);
  if (lang == "html" || lang == "svg" || lang == "mermaid") return true;
  if (lang == "xml" && core::lower(code).find("<svg") != std::string::npos) return true;
  return false;
}

bool App::firstArtifactIn(const std::string& content, std::string* code, std::string* language) {
  bool found = false;
  forEachFence(content, [&](const std::string& lang, const std::string& body) {
    if (found) return;
    // Mermaid needs the network, so it only opens on request.
    const std::string norm = normaliseLanguage(lang);
    if (norm == "mermaid" || !isArtifactLanguage(lang, body)) return;
    found = true;
    *code = body;
    *language = norm == "xml" ? "svg" : norm;
  });
  return found;
}

void App::scanArtifacts() {
  std::string key = chat.id;
  const std::vector<std::string> path = chat.branch();
  for (const std::string& id : path) {
    auto it = chat.messages.find(id);
    if (it == chat.messages.end()) continue;
    key += id.substr(0, 8) + ":" + std::to_string(it->second.content.size()) + ";";
  }
  if (key == artifactsKey_) return;
  artifactsKey_ = key;

  // Ad-hoc previews (no message) survive a rescan.
  std::vector<Artifact> kept;
  for (const Artifact& a : artifacts) {
    if (a.messageId.empty()) kept.push_back(a);
  }
  const std::string openCode = artifactIndex < static_cast<int>(artifacts.size())
                                   ? artifacts[static_cast<size_t>(artifactIndex)].code
                                   : "";
  artifacts.clear();
  int ordinal = 0;
  for (const std::string& id : path) {
    auto it = chat.messages.find(id);
    if (it == chat.messages.end() || it->second.role != "assistant") continue;
    forEachFence(it->second.content, [&](const std::string& lang, const std::string& body) {
      if (!isArtifactLanguage(lang, body)) return;
      Artifact a;
      a.messageId = id;
      a.language = normaliseLanguage(lang) == "xml" ? "svg" : normaliseLanguage(lang);
      a.code = body;
      a.title = titleFor(a.language, body, ++ordinal);
      artifacts.push_back(a);
    });
  }
  for (const Artifact& a : kept) artifacts.push_back(a);

  // Keep the open artifact selected when it is still there.
  artifactIndex = 0;
  for (size_t i = 0; i < artifacts.size(); ++i) {
    if (artifacts[i].code == openCode) artifactIndex = static_cast<int>(i);
  }
}

void App::openArtifact(const std::string& code, const std::string& language) {
  scanArtifacts();
  int index = -1;
  for (size_t i = 0; i < artifacts.size(); ++i) {
    if (artifacts[i].code == code) index = static_cast<int>(i);
  }
  if (index < 0) {
    Artifact a;
    a.language = normaliseLanguage(language) == "xml" ? "svg" : normaliseLanguage(language);
    a.code = code;
    a.title = titleFor(a.language, code, static_cast<int>(artifacts.size()) + 1);
    artifacts.push_back(a);
    index = static_cast<int>(artifacts.size()) - 1;
  }
  artifactIndex = index;
  artifactEdit = artifacts[static_cast<size_t>(index)].code;
  artifactEditing = false;
  showArtifacts = true;
  pane = Pane::Chat;
}

std::string App::artifactDocument(const Artifact& artifact) const {
  const bool dark = theme::isDark();
  const std::string bg = dark ? "#1b1b1f" : "#ffffff";
  const std::string fg = dark ? "#e6e6e6" : "#1a1a1a";
  if (artifact.language == "svg") {
    return "<!doctype html><html><head><meta charset=\"utf-8\"><style>html,body{margin:0;height:100%;background:" +
           bg + "}body{display:flex;align-items:center;justify-content:center}svg{max-width:100%;max-height:100vh}</style></head><body>" +
           artifact.code + "</body></html>";
  }
  if (artifact.language == "mermaid") {
    // Mermaid itself comes from a CDN: the diagram needs network access once.
    return "<!doctype html><html><head><meta charset=\"utf-8\"><style>html,body{margin:0;background:" + bg +
           ";color:" + fg + ";font-family:-apple-system,sans-serif}body{padding:16px}pre.mermaid{background:transparent}"
           ".err{color:#e55;white-space:pre-wrap;font:12px Menlo,monospace}</style></head><body>"
           "<pre class=\"mermaid\">" + escapeHtml(artifact.code) + "</pre>"
           "<script type=\"module\">import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs';"
           "mermaid.initialize({startOnLoad:false,theme:'" + (dark ? "dark" : "default") + "'});"
           "try{await mermaid.run();}catch(e){document.body.insertAdjacentHTML('beforeend','<div class=err>'+String(e.message||e).replace(/</g,'&lt;')+'</div>');console.error(String(e.message||e));}"
           "</script></body></html>";
  }
  // HTML fragments without a document get a body so they render in the
  // app's colours; full documents are left alone.
  const std::string lower = core::lower(artifact.code);
  if (lower.find("<html") == std::string::npos && lower.find("<body") == std::string::npos) {
    return "<!doctype html><html><head><meta charset=\"utf-8\"><style>body{background:" + bg + ";color:" + fg +
           ";font-family:-apple-system,sans-serif;margin:16px}</style></head><body>" + artifact.code +
           "</body></html>";
  }
  return artifact.code;
}

void App::drawArtifactColumn(float width) {
  const auto& palette = theme::palette();
  scanArtifacts();
  if (artifacts.empty()) {
    showArtifacts = false;
    return;
  }
  artifactIndex = std::clamp(artifactIndex, 0, static_cast<int>(artifacts.size()) - 1);
  Artifact& artifact = artifacts[static_cast<size_t>(artifactIndex)];

  ImGui::BeginChild("##artifacts", ImVec2(width, 0), ImGuiChildFlags_Borders);
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::Indent(10.0f);

  // Header: title, position among the chat's artifacts, close.
  theme::pushBold();
  ImGui::TextUnformatted(artifact.title.c_str());
  theme::pop();
  ImGui::SameLine();
  widgets::pill(artifact.language, palette.panelAlt, palette.textDim);
  if (artifacts.size() > 1) {
    ImGui::SameLine();
    if (ImGui::SmallButton("<") && artifactIndex > 0) {
      --artifactIndex;
      artifactEdit = artifacts[static_cast<size_t>(artifactIndex)].code;
      artifactEditing = false;
    }
    ImGui::SameLine();
    ImGui::Text("%d/%d", artifactIndex + 1, static_cast<int>(artifacts.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton(">") && artifactIndex + 1 < static_cast<int>(artifacts.size())) {
      ++artifactIndex;
      artifactEdit = artifacts[static_cast<size_t>(artifactIndex)].code;
      artifactEditing = false;
    }
  }
  ImGui::SameLine(width - 62.0f);
  if (ImGui::SmallButton("Close")) showArtifacts = false;

  // Actions.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  if (widgets::ghostButton(artifactEditing ? "Hide editor" : "Edit")) {
    artifactEditing = !artifactEditing;
    if (artifactEditing) artifactEdit = artifact.code;
  }
  ImGui::SameLine();
  if (widgets::ghostButton("Reload")) native::artifactReload();
  ImGui::SameLine();
  if (widgets::ghostButton("Copy")) {
    platform::setClipboardText(artifact.code);
    widgets::toastInfo("Copied");
  }
  ImGui::SameLine();
  if (widgets::ghostButton("Save...")) {
    const std::string ext = artifact.language == "mermaid" ? "mmd" : artifact.language;
    const std::string path = platform::saveFile("artifact." + ext);
    if (!path.empty()) {
      if (core::writeFile(path, artifact.code)) widgets::toast(widgets::ToastLevel::Success, "Saved " + core::baseName(path));
      else widgets::toastError("Could not write " + path);
    }
  }
  ImGui::SameLine();
  if (widgets::ghostButton("Open in browser")) {
    const std::string path = core::cacheDir() + "/artifact-" + std::to_string(std::hash<std::string>{}(artifact.code) % 100000) + ".html";
    if (core::writeFile(path, artifactDocument(artifact))) platform::openUrl("file://" + path);
    else widgets::toastError("Could not write " + path);
  }
  ImGui::PopStyleVar();
  ImGui::Unindent(10.0f);
  ImGui::Separator();

  // The web view takes what is left above the editor and the console line.
  const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
  const float editorHeight = artifactEditing ? 220.0f : 0.0f;
  const float consoleHeight = lineHeight + 6.0f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float viewWidth = ImGui::GetContentRegionAvail().x;
  const float viewHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y - editorHeight - consoleHeight - 8.0f);
  if (inSecondaryView()) {
    // One web view, in the main window (#121): offer to show this chat there.
    ImGui::BeginChild("##artifactelsewhere", ImVec2(viewWidth, viewHeight));
    widgets::dimText("%s", i18n::tr("The live preview runs in the main window."));
    if (ImGui::Button(i18n::tr("Open this chat in the main window"))) {
      const std::string id = chat.id;
      async::postMain([id] { App::instance().openChat(id); });   // runs as the main window
    }
    ImGui::EndChild();
  } else {
    native::artifactInstall();
    native::artifactSetFrame(pos.x, pos.y, viewWidth, viewHeight);
    artifactShownThisFrame = true;
    // What the view shows is keyed on the artifact's code and the theme, so an
    // edit, a different artifact or a theme switch reloads it.
    const std::string loadKey = artifact.language + "|" + (theme::isDark() ? "d" : "l") + "|" + artifact.code;
    if (loadKey != artifactLoadedKey_) {
      artifactLoadedKey_ = loadKey;
      native::artifactLoad(artifactDocument(artifact));
    }
    ImGui::Dummy(ImVec2(viewWidth, viewHeight));
  }

  if (artifactEditing) {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::InputTextMultiline("##artifactsrc", &artifactEdit, ImVec2(viewWidth, editorHeight - 48.0f),
                              ImGuiInputTextFlags_AllowTabInput);
    if (widgets::primaryButton("Apply", ImVec2(90, 0))) artifact.code = artifactEdit;
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(90, 0))) artifactEdit = artifact.code;
  }

  // Console: the last line, with everything on hover.
  const std::vector<std::string> console = native::artifactConsole();
  ImGui::Dummy(ImVec2(0, 2));
  if (console.empty()) {
    widgets::dimText("console: quiet");
  } else {
    const std::string& last = console.back();
    const bool bad = core::startsWith(last, "error") || core::startsWith(last, "unhandled") ||
                     core::startsWith(last, "load failed");
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(bad ? palette.danger : palette.textDim));
    ImGui::TextUnformatted(("console (" + std::to_string(console.size()) + "): " + last).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && console.size() > 1) {
      ImGui::BeginTooltip();
      for (const std::string& line : console) ImGui::TextUnformatted(line.c_str());
      ImGui::EndTooltip();
    }
  }
  ImGui::EndChild();
}

}  // namespace app
