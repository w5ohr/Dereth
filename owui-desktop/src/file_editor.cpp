// File editor window (#100): tabs of files, a syntax-highlighted editor
// (ImGui's multiline input with its text drawn transparent and the
// highlighter painted over it, so editing, selection and the built-in
// undo/redo stay native), a line-number gutter, find and go-to-line, Save /
// Revert, and the per-file snapshot history that spans applies.
#include <algorithm>

#include "app.h"
#include "core/common.h"
#include "core/fileeditor.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ui/highlight.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Tab {
  std::string path;
  std::string text;
  std::string saved;        // what is on disk (as last read or written)
  std::string language;
  std::vector<fileeditor::Snapshot> history;
  std::string find;
  size_t findPos = 0;
  int gotoLine = 0;
  int jumpTo = 0;           // a line the next frame scrolls to
  bool showHistory = true;
  bool missing = false;     // the file is not on disk yet
};
std::vector<Tab> gTabs;
size_t gActive = 0;
float gScrollY = 0.0f;      // the editor child's scroll, read after it draws

std::string historyDir() { return core::supportDir() + "/editor-history"; }

Tab* active() { return gTabs.empty() ? nullptr : &gTabs[std::min(gActive, gTabs.size() - 1)]; }

void loadTab(Tab& tab) {
  bool ok = false;
  tab.text = core::readFile(tab.path, &ok);
  tab.missing = !ok;
  if (!ok) tab.text.clear();
  tab.saved = tab.text;
  tab.language = fileeditor::languageFor(tab.path);
  tab.history = fileeditor::loadHistory(historyDir(), tab.path);
}

void saveTab(Tab& tab) {
  if (!core::writeFile(tab.path, tab.text)) {
    widgets::toastError("Could not write " + tab.path);
    return;
  }
  tab.saved = tab.text;
  tab.missing = false;
  fileeditor::record(historyDir(), tab.path, tab.text, "saved in the editor");
  tab.history = fileeditor::loadHistory(historyDir(), tab.path);
  widgets::toast(widgets::ToastLevel::Success, "Saved " + core::baseName(tab.path));
}

// Paints the highlighted text over the (transparent) input text. Re-enters
// the input's child window, whose origin minus scroll is where the input
// drew its text (WindowPadding is 0 there).
void paintHighlight(const char* inputLabel, const Tab& tab, bool dark) {
  ImGui::BeginChild(inputLabel);
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  gScrollY = window->Scroll.y;
  ImDrawList* dl = window->DrawList;
  const ImVec2 origin(window->Pos.x - window->Scroll.x, window->Pos.y - window->Scroll.y);
  const float lineH = ImGui::GetTextLineHeight();
  const float top = window->ClipRect.Min.y, bottom = window->ClipRect.Max.y;
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  bool inBlock = false;
  size_t start = 0;
  int lineNo = 0;
  while (start <= tab.text.size()) {
    const size_t nl = tab.text.find('\n', start);
    const std::string line = tab.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    const float y = origin.y + static_cast<float>(lineNo) * lineH;
    if (y + lineH >= top && y <= bottom && !line.empty()) {
      float x = origin.x;
      if (tab.language.empty()) {
        dl->AddText(font, size, ImVec2(x, y), theme::palette().text, line.c_str());
      } else {
        for (const highlight::Span& span : highlight::tokenize(line, tab.language, &inBlock)) {
          const char* b = line.c_str() + span.begin;
          const char* e = line.c_str() + span.end;
          dl->AddText(font, size, ImVec2(x, y), highlight::colorFor(span.kind, dark), b, e);
          x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, b, e).x;
        }
      }
    } else if (!tab.language.empty() && !line.empty()) {
      highlight::tokenize(line, tab.language, &inBlock);   // keep block-comment state in step
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
    ++lineNo;
  }
  ImGui::EndChild();
}

}  // namespace

std::vector<std::string> App::openEditorPaths() const {
  std::vector<std::string> out;
  for (const Tab& t : gTabs) out.push_back(t.path);
  return out;
}

void App::openFileEditor(const std::string& path, int line) {
  for (size_t i = 0; i < gTabs.size(); ++i) {
    if (gTabs[i].path == path) {
      gActive = i;
      gTabs[i].history = fileeditor::loadHistory(historyDir(), path);
      if (line > 0) gTabs[i].jumpTo = line;
      showFileEditor = true;
      return;
    }
  }
  Tab tab;
  tab.path = path;
  loadTab(tab);
  if (line > 0) tab.jumpTo = line;
  gTabs.push_back(std::move(tab));
  gActive = gTabs.size() - 1;
  showFileEditor = true;
}

void App::refreshEditorHistory(const std::string& path) {
  for (Tab& t : gTabs) {
    if (t.path == path) {
      t.history = fileeditor::loadHistory(historyDir(), path);
      bool ok = false;
      const std::string disk = core::readFile(path, &ok);
      if (ok && t.text == t.saved) {   // no unsaved edits: follow what the apply wrote
        t.text = disk;
        t.saved = disk;
      }
    }
  }
}

void App::drawFileEditor() {
  if (!showFileEditor) return;
  const auto& palette = theme::palette();
  ImGui::SetNextWindowSize(ImVec2(1120, 720), ImGuiCond_Appearing);
  if (!ImGui::Begin("Editor", &showFileEditor, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  if (gTabs.empty()) {
    widgets::dimText("No file open. Use \"Open a file in the editor\" from the palette, or Edit in the Apply dialog.");
    ImGui::End();
    return;
  }
  const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGuiIO& io = ImGui::GetIO();

  // Tabs.
  if (ImGui::BeginTabBar("##edtabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable)) {
    for (size_t i = 0; i < gTabs.size();) {
      Tab& t = gTabs[i];
      bool open = true;
      const std::string label = core::baseName(t.path) + (t.text != t.saved ? " *" : "") + "##" + t.path;
      if (ImGui::BeginTabItem(label.c_str(), &open, gActive == i ? ImGuiTabItemFlags_SetSelected : 0)) {
        gActive = i;
        ImGui::EndTabItem();
      }
      if (!open) {
        gTabs.erase(gTabs.begin() + static_cast<long>(i));
        if (gActive >= gTabs.size() && !gTabs.empty()) gActive = gTabs.size() - 1;
        continue;
      }
      ++i;
    }
    ImGui::EndTabBar();
  }
  Tab* tabPtr = active();
  if (!tabPtr) {
    ImGui::End();
    return;
  }
  Tab& tab = *tabPtr;

  // Toolbar.
  const bool dirty = tab.text != tab.saved;
  if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveTab(tab);
  if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) ImGui::SetKeyboardFocusHere(1);
  widgets::beginDisabled(!dirty && !tab.missing);
  if (widgets::primaryButton("Save", ImVec2(70, 0))) saveTab(tab);
  widgets::endDisabled(!dirty && !tab.missing);
  ImGui::SameLine();
  widgets::beginDisabled(!dirty);
  if (ImGui::Button("Revert to disk")) tab.text = tab.saved;
  widgets::endDisabled(!dirty);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  const bool findEnter = ImGui::InputTextWithHint("##find", "Find (Cmd+F, Enter = next)", &tab.find, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if ((ImGui::SmallButton("Next") || findEnter) && !tab.find.empty()) {
    const size_t pos = fileeditor::findNext(tab.text, tab.find, tab.findPos, true);
    if (pos == std::string::npos) {
      widgets::toastInfo("Not found: " + tab.find);
    } else {
      tab.findPos = pos + 1;
      tab.jumpTo = fileeditor::lineOf(tab.text, pos);
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  if (ImGui::InputInt("##goto", &tab.gotoLine, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) && tab.gotoLine > 0) tab.jumpTo = tab.gotoLine;
  ImGui::SameLine();
  widgets::dimText("line");
  ImGui::SameLine();
  ImGui::Checkbox("History", &tab.showHistory);
  ImGui::SameLine();
  widgets::dimText("%s - %d lines%s%s", tab.path.c_str(), fileeditor::lineCount(tab.text), dirty ? " - modified" : "", tab.missing ? " - not on disk yet" : "");

  // Editor: gutter + input + overlay; history on the right.
  const float historyW = tab.showHistory ? 300.0f : 0.0f;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float editorW = avail.x - historyW - (tab.showHistory ? 8.0f : 0.0f);
  const float gutterW = 48.0f;
  theme::pushMono();
  const float lineH = ImGui::GetTextLineHeight();
  // Gutter, offset by the editor's scroll from the last frame.
  ImGui::BeginChild("##gutter", ImVec2(gutterW, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const int lines = std::max(1, fileeditor::lineCount(tab.text) + (tab.text.empty() || tab.text.back() == '\n' ? 1 : 0));
    const float top = ImGui::GetWindowPos().y, bottom = top + ImGui::GetWindowHeight();
    for (int i = 0; i < lines; ++i) {
      const float y = base.y + static_cast<float>(i) * lineH - gScrollY;
      if (y + lineH < top || y > bottom) continue;
      const std::string n = std::to_string(i + 1);
      const float w = ImGui::CalcTextSize(n.c_str()).x;
      dl->AddText(ImVec2(base.x + gutterW - 10.0f - w, y), palette.textFaint, n.c_str());
    }
  }
  ImGui::EndChild();
  ImGui::SameLine(0.0f, 0.0f);
  // Jump: scroll the input's child so the target line is a third down.
  if (tab.jumpTo > 0) {
    ImGui::SetNextWindowScroll(ImVec2(-1.0f, std::max(0.0f, static_cast<float>(tab.jumpTo - 1) * lineH - avail.y * 0.33f)));
    tab.jumpTo = 0;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  ImGui::InputTextMultiline("##source", &tab.text, ImVec2(editorW - gutterW, avail.y), ImGuiInputTextFlags_AllowTabInput);
  ImGui::PopStyleColor();
  paintHighlight("##source", tab, theme::isDark());
  theme::pop();

  if (tab.showHistory) {
    ImGui::SameLine();
    ImGui::BeginChild("##edhistory", ImVec2(historyW, avail.y), ImGuiChildFlags_Borders);
    theme::pushBold();
    ImGui::TextUnformatted("History");
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("%d snapshot%s", static_cast<int>(tab.history.size()), tab.history.size() == 1 ? "" : "s");
    ImGui::PushTextWrapPos(historyW - 12.0f);
    widgets::dimText("Every apply records the file before and after it; saves here too. Restore puts a snapshot in the editor (the current text is kept as a snapshot first) - Save writes it.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (tab.history.empty()) widgets::dimText("No applies or saves recorded for this file yet.");
    const int64_t now = core::nowSeconds();
    for (size_t i = tab.history.size(); i-- > 0;) {
      const fileeditor::Snapshot& s = tab.history[i];
      ImGui::PushID(static_cast<int>(i));
      const bool current = s.content == tab.text;
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(current ? palette.accent : palette.text));
      ImGui::TextWrapped("%s", fileeditor::describe(s, now).c_str());
      ImGui::PopStyleColor();
      if (current) {
        widgets::dimText("  = the editor's text");
      } else {
        const int diffLine = fileeditor::firstDifferingLine(s.content, tab.text);
        widgets::dimText("  first difference at line %d", diffLine);
        ImGui::SameLine();
        if (ImGui::SmallButton("Restore")) {
          fileeditor::record(historyDir(), tab.path, tab.text, "before restore");
          tab.text = s.content;
          tab.history = fileeditor::loadHistory(historyDir(), tab.path);
          tab.jumpTo = std::max(1, diffLine);
          ImGui::PopID();
          break;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Go to")) tab.jumpTo = std::max(1, diffLine);
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

void App::drawFileEditorMenuEntry() {}

}  // namespace app
