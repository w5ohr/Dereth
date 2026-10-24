#include "ui/diffview.h"

#include <algorithm>
#include <cstdio>

#include "core/common.h"
#include "imgui.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace diffview {
namespace {

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines = core::split(text, '\n');
  if (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

bool parseHeader(const std::string& line, Hunk* hunk) {
  int os = 0, oc = 1, ns = 0, nc = 1;
  if (sscanf(line.c_str(), "@@ -%d,%d +%d,%d @@", &os, &oc, &ns, &nc) == 4 ||
      (sscanf(line.c_str(), "@@ -%d +%d,%d @@", &os, &ns, &nc) == 3 && (oc = 1)) ||
      (sscanf(line.c_str(), "@@ -%d,%d +%d @@", &os, &oc, &ns) == 3 && (nc = 1)) ||
      (sscanf(line.c_str(), "@@ -%d +%d @@", &os, &ns) == 2 && (oc = nc = 1))) {
    hunk->oldStart = os;
    hunk->oldCount = oc;
    hunk->newStart = ns;
    hunk->newCount = nc;
    hunk->header = line;
    return true;
  }
  return false;
}

std::string stripPrefix(std::string path) {
  path = core::trim(path);
  size_t tab = path.find('\t');
  if (tab != std::string::npos) path = path.substr(0, tab);
  if (core::startsWith(path, "a/") || core::startsWith(path, "b/")) path = path.substr(2);
  return path;
}

}  // namespace

bool looksLikeDiff(const std::string& text) {
  return text.find("\n@@ ") != std::string::npos || core::startsWith(text, "@@ ") ||
         (text.find("--- ") != std::string::npos && text.find("+++ ") != std::string::npos);
}

std::vector<FileDiff> parse(const std::string& unified) {
  std::vector<FileDiff> files;
  FileDiff current;
  Hunk* hunk = nullptr;
  bool haveFile = false;

  for (const std::string& raw : core::split(unified, '\n')) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (core::startsWith(line, "diff --git") || core::startsWith(line, "index ")) {
      continue;
    }
    if (core::startsWith(line, "--- ")) {
      if (haveFile && !current.hunks.empty()) {
        files.push_back(current);
        current = FileDiff();
      }
      current.oldPath = stripPrefix(line.substr(4));
      haveFile = true;
      hunk = nullptr;
      continue;
    }
    if (core::startsWith(line, "+++ ")) {
      current.newPath = stripPrefix(line.substr(4));
      continue;
    }
    if (core::startsWith(line, "@@")) {
      Hunk fresh;
      if (!parseHeader(line, &fresh)) continue;
      current.hunks.push_back(fresh);
      hunk = &current.hunks.back();
      haveFile = true;
      continue;
    }
    if (!hunk) continue;
    if (line.empty()) {
      // A bare blank line inside a hunk is a context line whose leading space
      // was dropped - the other failure mode we measured. Treat it as context.
      hunk->lines.push_back({' ', ""});
      continue;
    }
    if (line[0] == '\\') continue;   // "\ No newline at end of file"
    char tag = line[0];
    if (tag != ' ' && tag != '+' && tag != '-') {
      // Unmarked context line; keep it rather than dropping the hunk.
      hunk->lines.push_back({' ', line});
      continue;
    }
    hunk->lines.push_back({tag, line.substr(1)});
  }
  if (haveFile && !current.hunks.empty()) files.push_back(current);
  return files;
}

bool apply(const FileDiff& diff, const std::string& original, std::string* result,
           std::string* error) {
  std::vector<std::string> lines = splitLines(original);
  int offset = 0;   // accumulated drift from earlier hunks

  for (const Hunk& hunk : diff.hunks) {
    if (!hunk.accepted) continue;
    std::vector<std::string> before;
    std::vector<std::string> after;
    for (const Line& line : hunk.lines) {
      if (line.tag != '+') before.push_back(line.text);
      if (line.tag != '-') after.push_back(line.text);
    }

    auto matchesAt = [&](int at) {
      if (at < 0 || at + static_cast<int>(before.size()) > static_cast<int>(lines.size())) return false;
      for (size_t i = 0; i < before.size(); ++i) {
        if (core::trim(lines[at + i]) != core::trim(before[i])) return false;
      }
      return true;
    };

    int guess = std::max(0, hunk.oldStart - 1 + offset);
    int found = -1;
    // Try the stated position, then widen outwards, then scan the whole file.
    for (int radius = 0; radius < 200 && found < 0; ++radius) {
      if (matchesAt(guess + radius)) found = guess + radius;
      else if (radius && matchesAt(guess - radius)) found = guess - radius;
    }
    if (found < 0) {
      for (int at = 0; at + static_cast<int>(before.size()) <= static_cast<int>(lines.size()); ++at) {
        if (matchesAt(at)) {
          found = at;
          break;
        }
      }
    }
    if (found < 0 && before.empty()) found = guess;
    if (found < 0) {
      if (error) *error = "could not place hunk " + hunk.header;
      return false;
    }
    lines.erase(lines.begin() + found, lines.begin() + found + static_cast<long>(before.size()));
    lines.insert(lines.begin() + found, after.begin(), after.end());
    offset += static_cast<int>(after.size()) - static_cast<int>(before.size());
  }
  *result = core::join(lines, "\n") + "\n";
  return true;
}

std::string generate(const std::string& path, const std::string& beforeText,
                     const std::string& afterText, int context) {
  std::vector<std::string> a = splitLines(beforeText);
  std::vector<std::string> b = splitLines(afterText);
  const size_t n = a.size(), m = b.size();

  // LCS table; files are small enough that O(n*m) is fine here.
  std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      lcs[i][j] = a[i] == b[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }
  }
  struct Op { char tag; size_t ai; size_t bi; };
  std::vector<Op> ops;
  size_t i = 0, j = 0;
  while (i < n && j < m) {
    if (a[i] == b[j]) { ops.push_back({' ', i, j}); ++i; ++j; }
    else if (lcs[i + 1][j] >= lcs[i][j + 1]) { ops.push_back({'-', i, j}); ++i; }
    else { ops.push_back({'+', i, j}); ++j; }
  }
  while (i < n) { ops.push_back({'-', i, j}); ++i; }
  while (j < m) { ops.push_back({'+', i, j}); ++j; }

  std::string out = "--- a/" + path + "\n+++ b/" + path + "\n";
  size_t k = 0;
  while (k < ops.size()) {
    if (ops[k].tag == ' ') { ++k; continue; }
    size_t start = k >= static_cast<size_t>(context) ? k - context : 0;
    size_t end = k;
    size_t lastChange = k;
    while (end < ops.size()) {
      if (ops[end].tag != ' ') lastChange = end;
      else if (end - lastChange > static_cast<size_t>(2 * context)) break;
      ++end;
    }
    end = std::min(ops.size(), lastChange + context + 1);
    int oldStart = 0, newStart = 0, oldCount = 0, newCount = 0;
    for (size_t t = start; t < end; ++t) {
      if (ops[t].tag != '+') ++oldCount;
      if (ops[t].tag != '-') ++newCount;
    }
    oldStart = static_cast<int>(ops[start].ai) + 1;
    newStart = static_cast<int>(ops[start].bi) + 1;
    if (oldCount == 0) oldStart -= 1;
    if (newCount == 0) newStart -= 1;
    out += "@@ -" + std::to_string(oldStart) + "," + std::to_string(oldCount) + " +" +
           std::to_string(newStart) + "," + std::to_string(newCount) + " @@\n";
    for (size_t t = start; t < end; ++t) {
      const Op& op = ops[t];
      out += op.tag;
      out += op.tag == '+' ? b[op.bi] : a[op.ai];
      out += "\n";
    }
    k = end;
  }
  return out;
}

void render(const char* id, std::vector<FileDiff>& diffs, bool editable, float width) {
  ImGui::PushID(id);
  const auto& palette = theme::palette();
  const bool dark = theme::isDark();
  const ImU32 addBg = dark ? IM_COL32(40, 90, 50, 120) : IM_COL32(200, 240, 200, 160);
  const ImU32 delBg = dark ? IM_COL32(110, 45, 45, 120) : IM_COL32(250, 200, 200, 160);
  theme::pushMono();
  int fileIndex = 0;
  for (FileDiff& file : diffs) {
    ImGui::PushID(fileIndex++);
    theme::pushBold();
    ImGui::TextUnformatted((file.newPath.empty() ? file.oldPath : file.newPath).c_str());
    theme::pop();
    int hunkIndex = 0;
    for (Hunk& hunk : file.hunks) {
      ImGui::PushID(hunkIndex++);
      if (editable) {
        ImGui::Checkbox("##accept", &hunk.accepted);
        ImGui::SameLine();
      }
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.accent));
      ImGui::TextUnformatted(hunk.header.c_str());
      ImGui::PopStyleColor();
      for (const Line& line : hunk.lines) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        if (line.tag != ' ') {
          ImGui::GetWindowDrawList()->AddRectFilled(
              pos, ImVec2(pos.x + width, pos.y + ImGui::GetTextLineHeight()),
              line.tag == '+' ? addBg : delBg);
        }
        ImU32 color = line.tag == '+' ? palette.success
                      : (line.tag == '-' ? palette.danger : palette.textDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(color));
        std::string shown(1, line.tag);
        shown += line.text;
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::Dummy(ImVec2(0, 4));
      ImGui::PopID();
    }
    ImGui::PopID();
  }
  theme::pop();
  ImGui::PopID();
}

}  // namespace diffview
