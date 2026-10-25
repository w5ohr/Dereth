#include "core/i18n.h"
#include "ui/markdown.h"

#include <set>

#include <cmath>
#include <unordered_map>
#include <vector>

#include "core/common.h"
#include "core/editorbridge.h"
#include "imgui_internal.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/diffview.h"
#include "ui/highlight.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace md {
namespace {

struct Style {
  bool bold = false;
  bool italic = false;
  bool code = false;
  bool strike = false;
  std::string link;
};

struct Run {
  std::string text;
  Style style;
  bool image = false;
  std::string imageUrl;
};

// ---- inline parsing -------------------------------------------------------

bool matchDelimiter(const std::string& s, size_t i, const char* delim, size_t len) {
  return s.compare(i, len, delim, len) == 0;
}

size_t findClosing(const std::string& s, size_t from, const char* delim, size_t len) {
  for (size_t i = from; i + len <= s.size(); ++i) {
    if (s[i] == '\\') {
      ++i;
      continue;
    }
    if (matchDelimiter(s, i, delim, len)) return i;
  }
  return std::string::npos;
}

std::vector<Run> parseInline(const std::string& text) {
  std::vector<Run> runs;
  Style style;
  std::string buffer;

  auto flush = [&] {
    if (!buffer.empty()) {
      runs.push_back({buffer, style, false, ""});
      buffer.clear();
    }
  };

  for (size_t i = 0; i < text.size();) {
    const char c = text[i];

    if (c == '\\' && i + 1 < text.size()) {
      buffer += text[i + 1];
      i += 2;
      continue;
    }

    // Inline code wins over every other marker.
    if (c == '`') {
      size_t ticks = 1;
      while (i + ticks < text.size() && text[i + ticks] == '`') ++ticks;
      std::string fence(ticks, '`');
      size_t close = text.find(fence, i + ticks);
      if (close != std::string::npos) {
        flush();
        Style codeStyle = style;
        codeStyle.code = true;
        editorbridge::FileRef ref;
        if (editorbridge::parseFileRef(text.substr(i + ticks, close - i - ticks), &ref)) codeStyle.link = editorbridge::linkFor(ref);
        runs.push_back({text.substr(i + ticks, close - i - ticks), codeStyle, false, ""});
        i = close + ticks;
        continue;
      }
    }

    // Images and links.
    if ((c == '!' && i + 1 < text.size() && text[i + 1] == '[') || c == '[') {
      const bool isImage = (c == '!');
      const size_t bracketStart = isImage ? i + 1 : i;
      size_t close = text.find(']', bracketStart + 1);
      if (close != std::string::npos && close + 1 < text.size() && text[close + 1] == '(') {
        size_t urlEnd = text.find(')', close + 2);
        if (urlEnd != std::string::npos) {
          flush();
          std::string label = text.substr(bracketStart + 1, close - bracketStart - 1);
          std::string url = core::trim(text.substr(close + 2, urlEnd - close - 2));
          size_t space = url.find(' ');
          if (space != std::string::npos) url = url.substr(0, space);
          if (isImage) {
            runs.push_back({label, style, true, url});
          } else {
            Style linkStyle = style;
            linkStyle.link = url;
            runs.push_back({label.empty() ? url : label, linkStyle, false, ""});
          }
          i = urlEnd + 1;
          continue;
        }
      }
    }

    // Bare URLs.
    if ((c == 'h' && (text.compare(i, 7, "http://") == 0 || text.compare(i, 8, "https://") == 0))) {
      size_t end = i;
      while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
             text[end] != ')' && text[end] != '>') {
        ++end;
      }
      while (end > i && (text[end - 1] == '.' || text[end - 1] == ',')) --end;
      flush();
      Style linkStyle = style;
      linkStyle.link = text.substr(i, end - i);
      runs.push_back({linkStyle.link, linkStyle, false, ""});
      i = end;
      continue;
    }

    // File references ("src/app.cpp:120", "~/x.py:3:4") open in the editor.
    if (i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '(' || text[i - 1] == '[' || text[i - 1] == ',') {
      size_t end = i;
      while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_' || text[end] == '.' || text[end] == '/' ||
                                   text[end] == '~' || text[end] == '-' || text[end] == ':')) {
        ++end;
      }
      while (end > i && (text[end - 1] == '.' || text[end - 1] == ',' || text[end - 1] == ':')) --end;
      editorbridge::FileRef ref;
      if (end > i && text.substr(i, end - i).find(':') != std::string::npos && editorbridge::parseFileRef(text.substr(i, end - i), &ref)) {
        flush();
        Style linkStyle = style;
        linkStyle.link = editorbridge::linkFor(ref);
        runs.push_back({text.substr(i, end - i), linkStyle, false, ""});
        i = end;
        continue;
      }
    }

    // A marker closes an open run unconditionally; it only opens one when a
    // matching close exists further along.
    if (matchDelimiter(text, i, "***", 3) &&
        ((style.bold && style.italic) || findClosing(text, i + 3, "***", 3) != std::string::npos)) {
      flush();
      style.bold = !style.bold;
      style.italic = !style.italic;
      i += 3;
      continue;
    }
    if (matchDelimiter(text, i, "**", 2) || matchDelimiter(text, i, "__", 2)) {
      const std::string marker = text.substr(i, 2);
      if (style.bold || findClosing(text, i + 2, marker.c_str(), 2) != std::string::npos) {
        flush();
        style.bold = !style.bold;
        i += 2;
        continue;
      }
    }
    if (matchDelimiter(text, i, "~~", 2) &&
        (style.strike || findClosing(text, i + 2, "~~", 2) != std::string::npos)) {
      flush();
      style.strike = !style.strike;
      i += 2;
      continue;
    }
    if ((c == '*' || c == '_') &&
        (style.italic || (i + 1 < text.size() &&
                          !std::isspace(static_cast<unsigned char>(text[i + 1]))))) {
      const char delim[2] = {c, '\0'};
      if (style.italic || findClosing(text, i + 1, delim, 1) != std::string::npos) {
        flush();
        style.italic = !style.italic;
        i += 1;
        continue;
      }
    }

    buffer += c;
    ++i;
  }
  flush();
  return runs;
}

// ---- inline layout --------------------------------------------------------

struct Layout {
  float startX = 0.0f;
  float maxWidth = 0.0f;
  ImVec2 cursor{};
  float lineHeight = 0.0f;
  float totalHeight = 0.0f;
};

ImFont* fontFor(const Style& style) {
  if (style.code) return theme::fonts.mono;
  if (style.bold) return theme::fonts.bold;
  return theme::fonts.regular;
}

void newLine(Layout* layout) {
  layout->cursor.x = layout->startX;
  layout->cursor.y += layout->lineHeight;
  layout->totalHeight += layout->lineHeight;
}

void drawRun(ImDrawList* dl, Layout* layout, const Run& run, const Options& options,
             ImU32 baseColor) {
  ImFont* font = fontFor(run.style);
  const float fontSize = ImGui::GetFontSize();
  ImU32 color = baseColor;
  if (!run.style.link.empty()) color = theme::palette().accent;
  if (run.style.code) color = theme::palette().warning;

  // Split on spaces so we can wrap between words.
  size_t i = 0;
  const std::string& text = run.text;
  while (i < text.size()) {
    if (text[i] == '\n') {
      newLine(layout);
      ++i;
      continue;
    }
    size_t end = i;
    while (end < text.size() && text[end] != ' ' && text[end] != '\n') ++end;
    if (end == i) {  // a run of spaces
      while (end < text.size() && text[end] == ' ') ++end;
    }
    std::string word = text.substr(i, end - i);
    ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, word.c_str());
    if (layout->cursor.x + size.x > layout->startX + layout->maxWidth &&
        layout->cursor.x > layout->startX) {
      newLine(layout);
      if (word[0] == ' ') {  // drop leading spaces after a wrap
        i = end;
        continue;
      }
    }
    ImVec2 pos = layout->cursor;
    if (run.style.code) {
      dl->AddRectFilled(ImVec2(pos.x - 2, pos.y - 1), ImVec2(pos.x + size.x + 2, pos.y + size.y + 1),
                        theme::palette().codeBg, 3.0f);
    }
    dl->AddText(font, fontSize, pos, color, word.c_str());
    if (run.style.italic) {
      // stb_truetype has no synthetic oblique; mark italics with a soft underline.
      dl->AddLine(ImVec2(pos.x, pos.y + size.y - 1), ImVec2(pos.x + size.x, pos.y + size.y - 1),
                  (color & 0x00FFFFFF) | 0x40000000, 1.0f);
    }
    if (run.style.strike) {
      dl->AddLine(ImVec2(pos.x, pos.y + size.y * 0.55f),
                  ImVec2(pos.x + size.x, pos.y + size.y * 0.55f), color, 1.0f);
    }
    if (!run.style.link.empty()) {
      ImVec2 min = pos;
      ImVec2 max(pos.x + size.x, pos.y + size.y);
      dl->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), color, 1.0f);
      if (ImGui::IsMouseHoveringRect(min, max)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          if (options.onLink) {
            options.onLink(run.style.link);
          } else {
            platform::openUrl(run.style.link);
          }
        }
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(run.style.link.c_str());
        ImGui::EndTooltip();
      }
    }
    layout->cursor.x += size.x;
    i = end;
  }
}

// Renders one paragraph's worth of runs and advances the ImGui cursor.
float measureText(const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; }

// A paragraph whose first strong character is Hebrew, Arabic, ... : wrapped in
// logical order, each line reordered for display and right-aligned. Inline
// styling is dropped for such paragraphs (the reordering works on plain text).
void renderRtl(const std::string& text, float width, ImU32 color) {
  const float lineHeight = ImGui::GetTextLineHeight() * 1.28f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const auto lines = i18n::wrapWords(text, width, measureText);
  float y = 0.0f;
  for (const std::string& line : lines) {
    const std::string shown = i18n::visual(line);
    const float w = ImGui::CalcTextSize(shown.c_str()).x;
    dl->AddText(ImVec2(origin.x + std::max(0.0f, width - w), origin.y + y), color, shown.c_str());
    y += lineHeight;
  }
  ImGui::Dummy(ImVec2(width, std::max(lineHeight, y)));
}

void renderRuns(const std::vector<Run>& runs, const Options& options, ImU32 color) {
  const float width = options.wrapWidth > 0.0f ? options.wrapWidth
                                               : ImGui::GetContentRegionAvail().x;
  {
    std::string all;
    bool images = false;
    for (const Run& run : runs) {
      all += run.text;
      images = images || run.image;
    }
    if (!images && i18n::isRtl(all)) {
      renderRtl(all, width, color);
      return;
    }
  }
  Layout layout;
  layout.startX = ImGui::GetCursorScreenPos().x;
  layout.cursor = ImGui::GetCursorScreenPos();
  layout.maxWidth = width;
  layout.lineHeight = ImGui::GetTextLineHeight() * 1.28f;
  layout.totalHeight = layout.lineHeight;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();

  for (const Run& run : runs) {
    if (run.image) {
      // Flush the line, then draw the image on its own.
      if (layout.cursor.x > layout.startX) newLine(&layout);
      ImGui::Dummy(ImVec2(width, layout.totalHeight));
      layout.totalHeight = 0.0f;

      if (options.imageLoader) {
        const std::string url = run.imageUrl;
        auto loader = options.imageLoader;
        images::Entry entry = images::get(url, [loader, url] { return loader(url); });
        if (entry.ready()) {
          float displayWidth = std::min(width, static_cast<float>(entry.width));
          float scale = displayWidth / static_cast<float>(entry.width);
          ImGui::Image(entry.texture,
                       ImVec2(displayWidth, static_cast<float>(entry.height) * scale));
          if (ImGui::IsItemClicked()) platform::openUrl(url);
        } else if (entry.failed) {
          widgets::dimText("[image unavailable: %s]", run.imageUrl.c_str());
        } else {
          widgets::loadingRow("loading image...");
        }
      } else {
        widgets::dimText("[image] %s", run.imageUrl.c_str());
      }
      layout.cursor = ImGui::GetCursorScreenPos();
      layout.startX = layout.cursor.x;
      continue;
    }
    drawRun(dl, &layout, run, options, color);
  }
  if (layout.totalHeight > 0.0f) {
    ImGui::Dummy(ImVec2(width, layout.totalHeight));
  }
}

// ---- block rendering ------------------------------------------------------

// mermaid / dot / math blocks as images, with the source one click away.
std::string diagramKind(const std::string& language) {
  const std::string lang = core::lower(core::trim(language));
  if (lang == "mermaid" || lang == "mmd") return "mermaid";
  if (lang == "dot" || lang == "graphviz") return "dot";
  if (lang == "math" || lang == "latex" || lang == "tex" || lang == "katex") return "math";
  return "";
}

std::set<std::string> gDiagramSourceShown;

void renderCodeBlock(const std::string& code, const std::string& language, const Options& options, int blockIndex);

void renderDiagramBlock(const std::string& kind, const std::string& code, const std::string& language,
                        const Options& options, int blockIndex) {
  const auto& palette = theme::palette();
  const float width = options.wrapWidth > 0.0f ? options.wrapWidth : ImGui::GetContentRegionAvail().x;
  const float renderWidth = std::min(width, 820.0f);
  const bool dark = theme::isDark();
  const std::string key = "block:" + kind + ":" + std::to_string(std::hash<std::string>{}(code)) + ":" +
                          std::to_string(static_cast<int>(renderWidth)) + (dark ? ":d" : ":l");
  ImGui::PushID(blockIndex);
  auto loader = options.blockImage;
  images::Entry entry = images::get(key, [loader, kind, code, renderWidth] { return loader(kind, code, renderWidth); });
  const bool showSource = gDiagramSourceShown.count(key) > 0 || entry.failed;

  // A slim header: kind, then Source / Copy on the right.
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
  ImGui::TextUnformatted(kind == "math" ? "math" : kind.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine(width - 100.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));
  if (!entry.failed && ImGui::SmallButton(showSource ? "Hide source" : "Source")) {
    if (showSource) gDiagramSourceShown.erase(key);
    else gDiagramSourceShown.insert(key);
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());
  ImGui::PopStyleVar();

  if (entry.ready()) {
    const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
    const float naturalW = static_cast<float>(entry.width) / scale;
    const float displayW = std::min(width, naturalW);
    const float displayH = static_cast<float>(entry.height) / scale * (displayW / naturalW);
    ImGui::Image(entry.texture, ImVec2(displayW, displayH));
  } else if (entry.failed) {
    widgets::dimText("Could not render this %s block (needs the CDN for KaTeX / Mermaid / Viz.js) - source below.", kind.c_str());
  } else {
    widgets::loadingRow(kind == "math" ? "Typesetting..." : "Drawing diagram...");
  }
  if (showSource) renderCodeBlock(code, language.empty() ? kind : language, options, blockIndex + 10000);
  ImGui::PopID();
}

void renderCodeBlock(const std::string& code, const std::string& language, const Options& options,
                     int index) {
  ImGui::PushID(index);
  const auto& palette = theme::palette();
  const float width = options.wrapWidth > 0.0f ? options.wrapWidth
                                               : ImGui::GetContentRegionAvail().x;
  const std::string lang = highlight::normalizeLanguage(language);
  const bool isDiff = lang == "diff" || (lang.empty() && diffview::looksLikeDiff(code));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.codeBg));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));

  // Parsed diffs keep their accept/reject state between frames, keyed by the
  // block's text so a re-render of the same message does not reset choices.
  static std::unordered_map<std::string, std::vector<diffview::FileDiff>> parsedDiffs;
  std::vector<diffview::FileDiff>* diffs = nullptr;
  if (isDiff) {
    auto it = parsedDiffs.find(code);
    if (it == parsedDiffs.end()) it = parsedDiffs.emplace(code, diffview::parse(code)).first;
    if (!it->second.empty()) diffs = &it->second;
  }

  const float headerHeight = ImGui::GetFrameHeight() + 6.0f;
  std::vector<std::string> lines = core::split(code, '\n');
  if (!lines.empty() && lines.back().empty()) lines.pop_back();
  float bodyLines = static_cast<float>(lines.size());
  if (diffs) {
    bodyLines = 0.0f;
    for (const auto& file : *diffs) {
      bodyLines += 1.0f;
      for (const auto& hunk : file.hunks) bodyLines += 1.4f + hunk.lines.size();
    }
  }
  // Highlighted lines are laid out as items, so they take the item spacing too.
  const float bodyHeight =
      std::min(520.0f, bodyLines * ImGui::GetTextLineHeightWithSpacing() + 18.0f);

  ImGui::BeginChild("codeblock", ImVec2(width, headerHeight + bodyHeight), ImGuiChildFlags_Borders);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(language.empty() ? (isDiff ? "diff" : "code") : language.c_str());
  ImGui::PopStyleColor();

  const bool canRunOnServer = options.onRunCode && (lang == "python" || lang == "javascript");
  const float buttons = 54.0f + (canRunOnServer ? 36.0f : 0.0f) + (diffs && options.onApplyDiff ? 70.0f : 0.0f);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttons);
  if (diffs && options.onApplyDiff) {
    if (ImGui::SmallButton("Apply")) {
      // Re-emit only the accepted hunks.
      std::string filtered;
      for (const auto& file : *diffs) {
        filtered += "--- a/" + file.oldPath + "\n+++ b/" + (file.newPath.empty() ? file.oldPath : file.newPath) + "\n";
        for (const auto& hunk : file.hunks) {
          if (!hunk.accepted) continue;
          filtered += hunk.header + "\n";
          for (const auto& line : hunk.lines) filtered += std::string(1, line.tag) + line.text + "\n";
        }
      }
      options.onApplyDiff(filtered);
    }
    ImGui::SameLine();
  }
  if (icons::button("copy", icons::Icon::Copy, "Copy")) {
    platform::setClipboardText(code);
    widgets::toast(widgets::ToastLevel::Success, "Code copied");
  }
  if (canRunOnServer) {
    ImGui::SameLine();
    if (icons::button("run", icons::Icon::Play, "Run on the server")) {
      options.onRunCode(code, language);
    }
  }

  ImGui::BeginChild("codebody", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);
  theme::pushMono();
  if (diffs) {
    diffview::render("diff", *diffs, options.onApplyDiff != nullptr, width - 30.0f);
  } else {
    bool inBlockComment = false;
    for (const std::string& line : lines) {
      highlight::drawLine(line, lang, &inBlockComment, theme::isDark());
    }
  }
  theme::pop();
  ImGui::EndChild();

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  if (options.onCodeBlockActions) {
    // A child of the block's width keeps "fill the row" widgets (the output
    // box) inside the message column instead of running to the window edge.
    ImGui::BeginChild("codeactions", ImVec2(width, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize);
    options.onCodeBlockActions(code, language);
    ImGui::EndChild();
  }
  ImGui::PopID();
}

void renderTable(const std::vector<std::string>& rows, const Options& options, int index) {
  if (rows.empty()) return;
  auto cells = [](const std::string& row) {
    std::string trimmed = core::trim(row);
    if (!trimmed.empty() && trimmed.front() == '|') trimmed.erase(0, 1);
    if (!trimmed.empty() && trimmed.back() == '|') trimmed.pop_back();
    std::vector<std::string> out;
    for (const std::string& cell : core::split(trimmed, '|')) out.push_back(core::trim(cell));
    return out;
  };

  std::vector<std::string> header = cells(rows[0]);
  if (header.empty()) return;

  char id[32];
  snprintf(id, sizeof(id), "##mdtable%d", index);
  // Keep the table inside the message column instead of the whole window.
  const float tableWidth = options.wrapWidth > 0.0f ? options.wrapWidth
                                                    : ImGui::GetContentRegionAvail().x;
  if (ImGui::BeginTable(id, static_cast<int>(header.size()),
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(tableWidth, 0.0f))) {
    for (const std::string& column : header) ImGui::TableSetupColumn(column.c_str());
    ImGui::TableHeadersRow();
    for (size_t r = 1; r < rows.size(); ++r) {
      std::vector<std::string> row = cells(rows[r]);
      bool divider = true;
      for (const std::string& cell : row) {
        if (cell.find_first_not_of("-: ") != std::string::npos) divider = false;
      }
      if (divider) continue;
      ImGui::TableNextRow();
      for (size_t c = 0; c < header.size(); ++c) {
        ImGui::TableSetColumnIndex(static_cast<int>(c));
        const std::string text = c < row.size() ? row[c] : "";
        Options cellOptions = options;
        cellOptions.wrapWidth = ImGui::GetContentRegionAvail().x;
        renderRuns(parseInline(text), cellOptions, theme::palette().text);
      }
    }
    ImGui::EndTable();
  }
}

}  // namespace

void render(const char* id, const std::string& text, const Options& options) {
  ImGui::PushID(id);
  const auto& palette = theme::palette();
  std::vector<std::string> lines = core::split(text, '\n');

  std::vector<std::string> paragraph;
  int blockIndex = 0;

  auto flushParagraph = [&] {
    if (paragraph.empty()) return;
    renderRuns(parseInline(core::join(paragraph, " ")), options, palette.text);
    paragraph.clear();
    if (!options.compact) ImGui::Dummy(ImVec2(0, 3));
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string line = lines[i];
    std::string trimmed = core::trim(line);

    // Fenced code.
    if (core::startsWith(trimmed, "```") || core::startsWith(trimmed, "~~~")) {
      flushParagraph();
      const std::string fence = trimmed.substr(0, 3);
      std::string language = core::trim(trimmed.substr(3));
      std::string code;
      ++i;
      for (; i < lines.size(); ++i) {
        if (core::startsWith(core::trim(lines[i]), fence)) break;
        code += lines[i];
        code += "\n";
      }
      const std::string kind = options.blockImage ? diagramKind(language) : "";
      if (!kind.empty()) renderDiagramBlock(kind, code, language, options, blockIndex++);
      else renderCodeBlock(code, language, options, blockIndex++);
      ImGui::Dummy(ImVec2(0, 4));
      continue;
    }

    // Display math: a $$ ... $$ paragraph (one line or several).
    if (options.blockImage && core::startsWith(trimmed, "$$")) {
      flushParagraph();
      std::string body;
      std::string rest = core::trim(trimmed.substr(2));
      bool closed = false;
      if (rest.size() >= 2 && core::endsWith(rest, "$$")) {
        body = core::trim(rest.substr(0, rest.size() - 2));
        closed = true;
      } else {
        body = rest.empty() ? "" : rest + "\n";
        for (++i; i < lines.size(); ++i) {
          const std::string t = core::trim(lines[i]);
          if (core::endsWith(t, "$$")) {
            body += t.substr(0, t.size() - 2);
            closed = true;
            break;
          }
          body += lines[i] + "\n";
        }
      }
      if (closed && !core::trim(body).empty()) {
        renderDiagramBlock("math", body, "math", options, blockIndex++);
        ImGui::Dummy(ImVec2(0, 4));
        continue;
      }
    }

    if (trimmed.empty()) {
      flushParagraph();
      continue;
    }

    // Headings.
    if (trimmed[0] == '#') {
      size_t level = 0;
      while (level < trimmed.size() && trimmed[level] == '#') ++level;
      if (level <= 6 && level < trimmed.size() && trimmed[level] == ' ') {
        flushParagraph();
        const float scale = level == 1 ? 1.5f : (level == 2 ? 1.28f : 1.12f);
        theme::pushBold();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
        Options headingOptions = options;
        renderRuns(parseInline(core::trim(trimmed.substr(level))), headingOptions, palette.text);
        ImGui::PopFont();
        theme::pop();
        ImGui::Dummy(ImVec2(0, 3));
        continue;
      }
    }

    // Horizontal rule.
    if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
      flushParagraph();
      ImGui::Separator();
      continue;
    }

    // Tables.
    if (trimmed.find('|') != std::string::npos && i + 1 < lines.size() &&
        core::trim(lines[i + 1]).find("--") != std::string::npos &&
        core::trim(lines[i + 1]).find('|') != std::string::npos) {
      flushParagraph();
      std::vector<std::string> rows;
      while (i < lines.size() && core::trim(lines[i]).find('|') != std::string::npos) {
        rows.push_back(lines[i]);
        ++i;
      }
      --i;
      renderTable(rows, options, blockIndex++);
      ImGui::Dummy(ImVec2(0, 4));
      continue;
    }

    // Block quote.
    if (trimmed[0] == '>') {
      flushParagraph();
      std::string quote = core::trim(trimmed.substr(1));
      ImVec2 start = ImGui::GetCursorScreenPos();
      ImGui::Indent(10.0f);
      Options quoteOptions = options;
      quoteOptions.wrapWidth = (options.wrapWidth > 0.0f ? options.wrapWidth
                                                         : ImGui::GetContentRegionAvail().x);
      renderRuns(parseInline(quote), quoteOptions, palette.textDim);
      ImGui::Unindent(10.0f);
      ImGui::GetWindowDrawList()->AddRectFilled(
          start, ImVec2(start.x + 3.0f, ImGui::GetCursorScreenPos().y - 4.0f), palette.border, 2.0f);
      continue;
    }

    // Lists.
    size_t indent = line.find_first_not_of(" \t");
    if (indent == std::string::npos) indent = 0;
    bool bullet = trimmed.size() > 1 && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
                  trimmed[1] == ' ';
    bool ordered = false;
    size_t digits = 0;
    while (digits < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[digits]))) {
      ++digits;
    }
    if (digits > 0 && digits + 1 < trimmed.size() && trimmed[digits] == '.' &&
        trimmed[digits + 1] == ' ') {
      ordered = true;
    }
    if (bullet || ordered) {
      flushParagraph();
      const float levelIndent = 12.0f + (indent / 2) * 14.0f;
      ImGui::Indent(levelIndent);
      std::string content =
          core::trim(trimmed.substr(bullet ? 2 : digits + 2));
      // Task list checkbox.
      if (core::startsWith(content, "[ ] ") || core::startsWith(content, "[x] ") ||
          core::startsWith(content, "[X] ")) {
        const bool checked = content[1] != ' ';
        icons::inlineIcon(checked ? icons::Icon::Check : icons::Icon::Close,
                          checked ? palette.success : palette.textFaint, 0.8f);
        ImGui::SameLine(0, 6);
        content = content.substr(4);
      } else {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        if (bullet) {
          ImGui::GetWindowDrawList()->AddCircleFilled(
              ImVec2(pos.x + 3, pos.y + ImGui::GetTextLineHeight() * 0.5f), 2.5f, palette.textDim);
        } else {
          char marker[8];
          snprintf(marker, sizeof(marker), "%.*s.", static_cast<int>(digits), trimmed.c_str());
          ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y), palette.textDim, marker);
        }
        ImGui::Dummy(ImVec2(ordered ? 18.0f : 10.0f, 0));
        ImGui::SameLine(0, 4);
      }
      Options listOptions = options;
      listOptions.wrapWidth = ImGui::GetContentRegionAvail().x;
      renderRuns(parseInline(content), listOptions, palette.text);
      ImGui::Unindent(levelIndent);
      continue;
    }

    paragraph.push_back(trimmed);
  }
  flushParagraph();
  ImGui::PopID();
}

std::string toPlainText(const std::string& text) {
  std::string out;
  for (const Run& run : parseInline(text)) {
    if (run.image) continue;
    out += run.text;
  }
  out = core::replaceAll(out, "#", "");
  return core::trim(out);
}

}  // namespace md
