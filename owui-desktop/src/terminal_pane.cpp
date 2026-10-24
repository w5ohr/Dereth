// The Terminal tab of the Tools pane: the user's login shell on a pty,
// drawn from the term::Screen grid, with keyboard input, scrollback, mouse
// selection, and ways to hand what is on screen to the chat.
#include "app.h"

#include <algorithm>
#include <cstdlib>

#include "core/common.h"
#include "imgui_internal.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

ImU32 cellColor(uint32_t rgb, ImU32 fallback, bool dim) {
  if (rgb == term::kDefaultColor) return fallback;
  ImU32 color = IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
  if (dim) color = (color & 0x00FFFFFF) | (170u << 24);
  return color;
}

void appendUtf8(std::string* out, unsigned int cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Keys with a fixed escape sequence. Arrows honour DECCKM (application mode).
std::string keySequence(ImGuiKey key, bool appCursor) {
  const char* csi = appCursor ? "\x1bO" : "\x1b[";
  switch (key) {
    case ImGuiKey_UpArrow: return std::string(csi) + "A";
    case ImGuiKey_DownArrow: return std::string(csi) + "B";
    case ImGuiKey_RightArrow: return std::string(csi) + "C";
    case ImGuiKey_LeftArrow: return std::string(csi) + "D";
    case ImGuiKey_Home: return "\x1b[H";
    case ImGuiKey_End: return "\x1b[F";
    case ImGuiKey_Delete: return "\x1b[3~";
    case ImGuiKey_PageUp: return "\x1b[5~";
    case ImGuiKey_PageDown: return "\x1b[6~";
    case ImGuiKey_Insert: return "\x1b[2~";
    case ImGuiKey_Enter: case ImGuiKey_KeypadEnter: return "\r";
    case ImGuiKey_Backspace: return "\x7f";
    case ImGuiKey_Tab: return "\t";
    case ImGuiKey_Escape: return "\x1b";
    default: return "";
  }
}

}  // namespace

void App::startTerminal() {
  if (!terminal) terminal = std::make_unique<core::Pty>();
  const char* shell = std::getenv("SHELL");
  std::vector<std::string> argv = {shell && *shell ? shell : "/bin/zsh", "-l", "-i"};
  terminalCwd = projectFolder.empty() ? native::homeDirectory() : projectFolder;
  terminalScreen.reset();
  terminalScroll = 0;
  terminalHasSelection = false;
  terminalError.clear();
  std::string error;
  if (!terminal->start(argv, terminalCwd, terminalScreen.cols(), terminalScreen.rows(), &error)) {
    terminalError = error;
  }
}

// Lines are addressed as scrollback first, then the live screen.
static const term::Line* absoluteLine(const term::Screen& screen, int index) {
  const int sb = static_cast<int>(screen.scrollback().size());
  if (index < 0) return nullptr;
  if (index < sb) return &screen.scrollback()[static_cast<size_t>(index)];
  if (index - sb < screen.rows()) return &screen.row(index - sb);
  return nullptr;
}

std::string App::terminalSelectionText() const {
  if (!terminalHasSelection) return "";
  int l0 = terminalSelAnchorLine, c0 = terminalSelAnchorCol;
  int l1 = terminalSelEndLine, c1 = terminalSelEndCol;
  if (l1 < l0 || (l1 == l0 && c1 < c0)) {
    std::swap(l0, l1);
    std::swap(c0, c1);
  }
  std::string out;
  for (int l = l0; l <= l1; ++l) {
    const term::Line* line = absoluteLine(terminalScreen, l);
    if (!line) continue;
    const std::string text = term::Screen::lineText(*line);
    // Columns are cells; the text is UTF-8, so walk by code point.
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); ++i) {
      if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) starts.push_back(i);
    }
    starts.push_back(text.size());
    const int from = l == l0 ? std::min<int>(c0, static_cast<int>(starts.size()) - 1) : 0;
    const int to = l == l1 ? std::min<int>(c1 + 1, static_cast<int>(starts.size()) - 1)
                           : static_cast<int>(starts.size()) - 1;
    if (to > from) out += text.substr(starts[static_cast<size_t>(from)], starts[static_cast<size_t>(to)] - starts[static_cast<size_t>(from)]);
    if (l != l1) out += "\n";
  }
  return out;
}

std::string App::terminalRecentText(int lines) const {
  const int total = static_cast<int>(terminalScreen.scrollback().size()) + terminalScreen.rows();
  std::vector<std::string> out;
  for (int l = std::max(0, total - lines); l < total; ++l) {
    const term::Line* line = absoluteLine(terminalScreen, l);
    if (line) out.push_back(term::Screen::lineText(*line));
  }
  while (!out.empty() && out.back().empty()) out.pop_back();
  return core::join(out, "\n");
}

void App::drawTerminalSection(float width) {
  const auto& palette = theme::palette();
  ImGuiIO& io = ImGui::GetIO();

  if (!terminal) startTerminal();
  const bool alive = terminal && terminal->alive();

  // Toolbar.
  theme::pushMono();
  ImGui::TextUnformatted(terminalCwd.c_str());
  theme::pop();
  ImGui::SameLine();
  if (alive) {
    widgets::pill(terminalScreen.title().empty() ? "running" : terminalScreen.title().substr(0, 40),
                  palette.panelAlt, palette.success);
  } else {
    widgets::pill("exited" + (terminal && terminal->exitCode() >= 0 ? " (" + std::to_string(terminal->exitCode()) + ")" : std::string()),
                  palette.panelAlt, palette.danger);
  }
  ImGui::SameLine();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  if (widgets::ghostButton(alive ? "Restart" : "Start")) {
    if (terminal) terminal->stop();
    startTerminal();
  }
  ImGui::SameLine();
  if (widgets::ghostButton("Clear")) {
    terminalScreen.feed("\x1b[2J\x1b[3J\x1b[H");
    terminalScroll = 0;
    terminalHasSelection = false;
  }
  ImGui::SameLine();
  widgets::beginDisabled(!terminalHasSelection);
  if (widgets::ghostButton("Copy")) {
    platform::setClipboardText(terminalSelectionText());
    widgets::toastInfo("Copied");
  }
  widgets::endDisabled(!terminalHasSelection);
  ImGui::SameLine();
  if (widgets::ghostButton(terminalHasSelection ? "Ask about selection" : "Ask about the last 40 lines")) {
    const std::string text = terminalHasSelection ? terminalSelectionText() : terminalRecentText(40);
    if (!core::trim(text).empty()) {
      composer = "Terminal output:\n```\n" + text + "\n```\n\n";
      ++composerGeneration;
      pane = Pane::Chat;
      focusComposer = true;
    }
  }
  ImGui::SameLine();
  if (widgets::ghostButton("Explain as build error")) {
    const std::string text = terminalHasSelection ? terminalSelectionText() : terminalRecentText(60);
    if (!core::trim(text).empty()) {
      pane = Pane::Chat;
      explainBuildError(text);
    }
  }
  ImGui::PopStyleVar();
  if (!terminalError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
    ImGui::TextUnformatted(terminalError.c_str());
    ImGui::PopStyleColor();
  }
  widgets::dimText("Click to type. Cmd+C copies a selection, Cmd+V pastes; scroll for history.");

  // The grid.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.codeBg));
  ImGui::BeginChild("##terminal", ImVec2(width, -4.0f), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  theme::pushMono();
  const ImVec2 glyph = ImGui::CalcTextSize("M");
  const float lineHeight = ImGui::GetTextLineHeight();
  const ImVec2 origin = ImVec2(ImGui::GetCursorScreenPos().x + 4.0f, ImGui::GetCursorScreenPos().y + 2.0f);
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const int cols = std::max(10, static_cast<int>((avail.x - 8.0f) / glyph.x));
  const int rows = std::max(3, static_cast<int>((avail.y - 4.0f) / lineHeight));
  if (cols != terminalScreen.cols() || rows != terminalScreen.rows()) {
    terminalScreen.resize(cols, rows);
    if (terminal) terminal->resize(cols, rows);
  }
  if (terminal) {
    const std::string bytes = terminal->drain();
    if (!bytes.empty()) {
      terminalScreen.feed(bytes);
      if (terminalScroll > 0) {
        // Keep the view where it was while history grows underneath.
        terminalScroll = std::min(terminalScroll, static_cast<int>(terminalScreen.scrollback().size()));
      }
    }
  }
  const int scrollbackSize = static_cast<int>(terminalScreen.scrollback().size());
  terminalScroll = std::clamp(terminalScroll, 0, scrollbackSize);

  // Mouse: focus, wheel, selection.
  ImGui::InvisibleButton("##grid", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
  const bool hovered = ImGui::IsItemHovered();
  // Own the wheel while hovered, or ImGui scrolls the Tools pane instead.
  if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
  if (ImGui::IsItemActivated()) terminalFocused = true;
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) terminalFocused = false;
  if (hovered && io.MouseWheel != 0.0f) {
    terminalScroll = std::clamp(terminalScroll + static_cast<int>(io.MouseWheel * 3.0f), 0, scrollbackSize);
  }
  auto cellAt = [&](ImVec2 pos, int* line, int* col) {
    *col = std::clamp(static_cast<int>((pos.x - origin.x) / glyph.x), 0, cols - 1);
    const int row = std::clamp(static_cast<int>((pos.y - origin.y) / lineHeight), 0, rows - 1);
    *line = scrollbackSize - terminalScroll + row;
  };
  if (ImGui::IsItemActivated()) {
    cellAt(io.MousePos, &terminalSelAnchorLine, &terminalSelAnchorCol);
    terminalSelEndLine = terminalSelAnchorLine;
    terminalSelEndCol = terminalSelAnchorCol;
    terminalSelecting = true;
    terminalHasSelection = false;
  }
  if (terminalSelecting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    cellAt(io.MousePos, &terminalSelEndLine, &terminalSelEndCol);
    terminalHasSelection = terminalSelEndLine != terminalSelAnchorLine || terminalSelEndCol != terminalSelAnchorCol;
  }
  if (terminalSelecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) terminalSelecting = false;

  // Keyboard: everything goes to the shell while the grid has focus. ImGui's
  // own keyboard navigation would otherwise eat Space and Enter.
  if (terminalFocused) {
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    std::string input;
    if (!io.KeySuper) {
      for (const ImWchar ch : io.InputQueueCharacters) {
        if (ch >= 0x20 && !io.KeyCtrl) appendUtf8(&input, static_cast<unsigned int>(ch));
      }
      io.InputQueueCharacters.resize(0);
      static const ImGuiKey kSpecial[] = {
          ImGuiKey_UpArrow,  ImGuiKey_DownArrow, ImGuiKey_RightArrow, ImGuiKey_LeftArrow, ImGuiKey_Home,
          ImGuiKey_End,      ImGuiKey_Delete,    ImGuiKey_PageUp,     ImGuiKey_PageDown,  ImGuiKey_Insert,
          ImGuiKey_Enter,    ImGuiKey_KeypadEnter, ImGuiKey_Backspace, ImGuiKey_Tab,      ImGuiKey_Escape,
      };
      for (ImGuiKey key : kSpecial) {
        if (ImGui::IsKeyPressed(key, true)) input += keySequence(key, terminalScreen.appCursorKeys());
      }
      if (io.KeyCtrl) {
        for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
          if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), true)) {
            input.push_back(static_cast<char>(k - ImGuiKey_A + 1));
          }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) input.push_back('\x1b');
        if (ImGui::IsKeyPressed(ImGuiKey_Backslash, true)) input.push_back('\x1c');
      }
    } else {
      // Cmd+C copies the selection, Cmd+V pastes (the Edit menu replays
      // these chords into ImGui).
      if (ImGui::IsKeyPressed(ImGuiKey_C, false) && terminalHasSelection) {
        platform::setClipboardText(terminalSelectionText());
      }
      if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        input += platform::clipboardText();
      }
    }
    if (!input.empty() && terminal) {
      terminal->write(input);
      terminalScroll = 0;
    }
  } else {
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  }

  // Draw.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 defaultFg = palette.text;
  const ImU32 defaultBg = palette.codeBg;
  const ImU32 selectionBg = IM_COL32(80, 120, 200, 110);
  int selL0 = terminalSelAnchorLine, selC0 = terminalSelAnchorCol, selL1 = terminalSelEndLine, selC1 = terminalSelEndCol;
  if (selL1 < selL0 || (selL1 == selL0 && selC1 < selC0)) {
    std::swap(selL0, selL1);
    std::swap(selC0, selC1);
  }
  const int firstLine = scrollbackSize - terminalScroll;
  std::string run;
  for (int r = 0; r < rows; ++r) {
    const term::Line* line = absoluteLine(terminalScreen, firstLine + r);
    if (!line) break;
    const float y = origin.y + r * lineHeight;
    const int absLine = firstLine + r;
    // Backgrounds and selection first, then text runs of one style.
    for (int c = 0; c < cols && c < static_cast<int>(line->size()); ++c) {
      const term::Cell& cell = (*line)[static_cast<size_t>(c)];
      const bool inverse = cell.flags & term::Inverse;
      ImU32 bg = inverse ? cellColor(cell.fg, defaultFg, false) : cellColor(cell.bg, defaultBg, false);
      const bool selected = terminalHasSelection &&
                            (absLine > selL0 || (absLine == selL0 && c >= selC0)) &&
                            (absLine < selL1 || (absLine == selL1 && c <= selC1));
      if (bg != defaultBg) {
        dl->AddRectFilled(ImVec2(origin.x + c * glyph.x, y), ImVec2(origin.x + (c + 1) * glyph.x, y + lineHeight), bg);
      }
      if (selected) {
        dl->AddRectFilled(ImVec2(origin.x + c * glyph.x, y), ImVec2(origin.x + (c + 1) * glyph.x, y + lineHeight), selectionBg);
      }
    }
    int c = 0;
    while (c < cols && c < static_cast<int>(line->size())) {
      const term::Cell& first = (*line)[static_cast<size_t>(c)];
      int end = c;
      run.clear();
      while (end < cols && end < static_cast<int>(line->size())) {
        const term::Cell& cell = (*line)[static_cast<size_t>(end)];
        if (cell.fg != first.fg || cell.flags != first.flags || cell.bg != first.bg) break;
        appendUtf8(&run, cell.ch ? cell.ch : U' ');
        ++end;
      }
      const bool inverse = first.flags & term::Inverse;
      ImU32 fg = inverse ? cellColor(first.bg, defaultBg, false)
                         : cellColor(first.fg, defaultFg, first.flags & term::Dim);
      if ((first.flags & term::Bold) && first.fg == term::kDefaultColor && !inverse) fg = IM_COL32(255, 255, 255, 255);
      if (run.find_first_not_of(' ') != std::string::npos) {
        dl->AddText(ImVec2(origin.x + c * glyph.x, y), fg, run.c_str());
      }
      if (first.flags & term::Underline) {
        dl->AddLine(ImVec2(origin.x + c * glyph.x, y + lineHeight - 1.0f),
                    ImVec2(origin.x + end * glyph.x, y + lineHeight - 1.0f), fg, 1.0f);
      }
      c = end;
    }
  }
  // Cursor, only when looking at the live screen.
  if (terminalScroll == 0 && terminalScreen.cursorVisible() && alive) {
    const ImVec2 a(origin.x + terminalScreen.cursorX() * glyph.x, origin.y + terminalScreen.cursorY() * lineHeight);
    const ImVec2 b(a.x + glyph.x, a.y + lineHeight);
    if (terminalFocused) {
      dl->AddRectFilled(a, b, IM_COL32(200, 200, 200, 190));
      const term::Cell& under = terminalScreen.row(terminalScreen.cursorY())[static_cast<size_t>(terminalScreen.cursorX())];
      std::string ch;
      appendUtf8(&ch, under.ch ? under.ch : U' ');
      dl->AddText(a, IM_COL32(20, 20, 20, 255), ch.c_str());
    } else {
      dl->AddRect(a, b, IM_COL32(200, 200, 200, 190));
    }
  }
  if (terminalScroll > 0) {
    char label[48];
    snprintf(label, sizeof(label), "%d lines back", terminalScroll);
    const ImVec2 size = ImGui::CalcTextSize(label);
    const ImVec2 p(origin.x + avail.x - size.x - 16.0f, origin.y + 4.0f);
    dl->AddRectFilled(ImVec2(p.x - 6, p.y - 2), ImVec2(p.x + size.x + 6, p.y + size.y + 2), palette.panelAlt, 4.0f);
    dl->AddText(p, palette.textDim, label);
  }
  theme::pop();
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

}  // namespace app
