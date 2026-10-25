// Playgrounds in the app (#99): "Try it" under a regex, SQL or jq block opens
// a modal where the code runs locally against sample text, a CSV or SQLite
// file, or JSON - results as a table or text, copyable or sent back to chat.
#include <sys/stat.h>

#include <algorithm>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/playground.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "imgui_stdlib.h"

namespace app {
namespace {

playground::Kind gKind = playground::Kind::Regex;
std::string gCode, gInput, gReplacement, gSqlitePath, gFileLabel, gTableName = "data";
std::string gOutput;
playground::RegexResult gRegex;
playground::SqlResult gSql;
bool gRan = false, gRunning = false, gRawJq = false;
int gGeneration = 0;

std::string baseName(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem(const std::string& name) {
  const size_t dot = name.rfind('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

void loadSamples() {
  gInput = playground::sampleInput(gKind);
  if (gCode.empty()) gCode = playground::sampleCode(gKind);
  gSqlitePath.clear();
  gFileLabel.clear();
  gTableName = "data";
}

std::string resultText(bool markdown) {
  switch (gKind) {
    case playground::Kind::Regex: {
      if (!gRegex.ok) return gRegex.error;
      std::string out = playground::matchesText(gRegex);
      if (!gRegex.replaced.empty()) out += "\nReplaced:\n" + gRegex.replaced;
      return out;
    }
    case playground::Kind::Sql: {
      if (!gSql.ok) return gSql.error;
      std::string out;
      for (const playground::Table& t : gSql.tables) out += (markdown ? playground::tableMarkdown(t) : playground::tableCsv(t)) + "\n";
      if (gSql.tables.empty()) out = std::to_string(gSql.changes) + " row(s) changed";
      return out;
    }
    default: return gOutput;
  }
}

}  // namespace

void App::openPlayground(const std::string& code, const std::string& language) {
  playground::Kind kind = playground::kindFor(language, code);
  if (kind == playground::Kind::None) kind = gKind == playground::Kind::None ? playground::Kind::Regex : gKind;
  const bool kindChanged = kind != gKind;
  gKind = kind;
  gCode = code.empty() ? playground::sampleCode(kind) : code;
  if (kindChanged || gInput.empty()) loadSamples();
  gRan = false;
  gOutput.clear();
  showPlayground = true;
}

void App::runPlayground() {
  gRan = true;
  gOutput.clear();
  switch (gKind) {
    case playground::Kind::Regex:
      gRegex = playground::runRegex(gCode, gInput, gReplacement);
      break;
    case playground::Kind::Sql:
      gSql = playground::runSql(gCode, gSqlitePath.empty() ? gInput : "", gTableName, gSqlitePath);
      break;
    case playground::Kind::Jq: {
      if (!playground::jqAvailable()) {
        gOutput = "jq is not installed. macOS 15 and later ship it at /usr/bin/jq; otherwise: brew install jq";
        break;
      }
      const std::string dir = core::supportDir() + "/playground";
      mkdir(dir.c_str(), 0700);
      const std::string path = dir + "/input.json";
      core::writeFile(path, gInput);
      gRunning = true;
      const int gen = ++gGeneration;
      const std::vector<std::string> argv = playground::jqArgv(gCode, path, gRawJq);
      async::run([gen, argv, dir] {
        const native::ProcessResult r = native::runProcess(argv, dir, 15, true);
        async::post([gen, r] {
          if (gen != gGeneration) return;
          gRunning = false;
          gOutput = r.timedOut ? "jq timed out after 15 s" : r.output;
          if (!r.timedOut && r.exitCode != 0) gOutput = "jq exited with " + std::to_string(r.exitCode) + "\n" + r.output;
        });
      });
      break;
    }
    default: break;
  }
}

void App::drawPlayground() {
  if (!showPlayground) return;
  ImGui::OpenPopup("Playground");
  ImGui::SetNextWindowSize(ImVec2(1000, 660), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Playground", &showPlayground)) return;
  const auto& palette = theme::palette();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    showPlayground = false;
    ImGui::CloseCurrentPopup();
  }
  if (ImGui::GetIO().KeySuper && ImGui::IsKeyPressed(ImGuiKey_Enter)) runPlayground();

  // Kind, file, samples, run.
  static const char* kKinds[] = {"Regex", "SQL", "jq"};
  int kindIndex = gKind == playground::Kind::Sql ? 1 : gKind == playground::Kind::Jq ? 2 : 0;
  ImGui::SetNextItemWidth(110);
  if (ImGui::Combo("##pgkind", &kindIndex, kKinds, 3)) {
    gKind = kindIndex == 1 ? playground::Kind::Sql : kindIndex == 2 ? playground::Kind::Jq : playground::Kind::Regex;
    gCode = playground::sampleCode(gKind);
    loadSamples();
    gRan = false;
  }
  ImGui::SameLine();
  if (ImGui::Button(gKind == playground::Kind::Sql ? "Load CSV or SQLite..." : gKind == playground::Kind::Jq ? "Load JSON..." : "Load text...")) {
    const std::vector<std::string> picked = platform::openFiles(false);
    if (!picked.empty()) {
      const std::string path = picked[0];
      const std::string lower = core::lower(path);
      if (gKind == playground::Kind::Sql && (core::endsWith(lower, ".sqlite") || core::endsWith(lower, ".db") || core::endsWith(lower, ".sqlite3"))) {
        gSqlitePath = path;
        gFileLabel = baseName(path);
      } else {
        bool ok = false;
        const std::string text = core::readFile(path, &ok);
        if (!ok) widgets::toastError("Could not read " + path);
        else {
          gInput = text.size() > 4 * 1024 * 1024 ? text.substr(0, 4 * 1024 * 1024) : text;
          gSqlitePath.clear();
          gFileLabel = baseName(path);
          gTableName = playground::identifier(stem(baseName(path)));
        }
      }
      gRan = false;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Sample data")) {
    loadSamples();
    gRan = false;
  }
  ImGui::SameLine();
  widgets::beginDisabled(gRunning);
  if (widgets::primaryButton(gRunning ? "Running..." : "Run  (Cmd+Enter)", ImVec2(160, 0))) runPlayground();
  widgets::endDisabled(gRunning);
  ImGui::SameLine();
  widgets::dimText("Everything runs on this Mac: std::regex (ECMAScript), SQLite in memory or read-only, jq in a sandbox.");

  // Editors.
  const float half = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
  const float editorHeight = gKind == playground::Kind::Regex ? 150.0f : 190.0f;
  ImGui::BeginChild("##pgcode", ImVec2(half, editorHeight + 54.0f));
  widgets::dimText("%s", gKind == playground::Kind::Regex ? "Pattern  (bare or /pattern/flags - i ignores case, m is multiline; groups as $1, $2)"
                                                           : gKind == playground::Kind::Sql ? "SQL  (several statements are fine)" : "jq filter");
  theme::pushMono();
  ImGui::InputTextMultiline("##pgcodeedit", &gCode, ImVec2(-1, editorHeight));
  theme::pop();
  if (gKind == playground::Kind::Regex) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pgrepl", "Replacement (optional) - e.g. $3/$2/$1", &gReplacement);
  } else if (gKind == playground::Kind::Jq) {
    ImGui::Checkbox("Raw output (-r)", &gRawJq);
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##pginput", ImVec2(half, editorHeight + 54.0f));
  if (gKind == playground::Kind::Sql) {
    if (!gSqlitePath.empty()) {
      widgets::dimText("SQLite file %s (read-only)", gFileLabel.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Use CSV instead")) {
        gSqlitePath.clear();
        gFileLabel.clear();
      }
    } else {
      widgets::dimText("CSV%s - queried as table \"%s\" (header row = columns; numeric columns typed)", gFileLabel.empty() ? "" : (" from " + gFileLabel).c_str(), gTableName.c_str());
    }
  } else if (gKind == playground::Kind::Jq) {
    widgets::dimText("JSON input%s", gFileLabel.empty() ? "" : (" from " + gFileLabel).c_str());
  } else {
    widgets::dimText("Sample text%s", gFileLabel.empty() ? "" : (" from " + gFileLabel).c_str());
  }
  if (gSqlitePath.empty()) {
    theme::pushMono();
    ImGui::InputTextMultiline("##pginputedit", &gInput, ImVec2(-1, editorHeight + 28.0f));
    theme::pop();
  }
  ImGui::EndChild();

  // Results.
  ImGui::Separator();
  ImGui::BeginChild("##pgresult", ImVec2(0, -34.0f));
  if (!gRan) {
    widgets::dimText("Run to see matches, rows or output here.");
  } else if (gKind == playground::Kind::Regex) {
    if (!gRegex.ok) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::TextWrapped("%s", gRegex.error.c_str());
      ImGui::PopStyleColor();
    } else {
      theme::pushBold();
      ImGui::Text("%zu match%s", gRegex.matches.size(), gRegex.matches.size() == 1 ? "" : "es");
      theme::pop();
      size_t groups = 0;
      for (const auto& m : gRegex.matches) groups = std::max(groups, m.groups.size());
      const int cols = 3 + static_cast<int>(std::min<size_t>(groups, 9));
      if (!gRegex.matches.empty() && ImGui::BeginTable("##pgmatches", cols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Span", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Match");
        for (int g = 3; g < cols; ++g) ImGui::TableSetupColumn(("$" + std::to_string(g - 2)).c_str());
        ImGui::TableHeadersRow();
        int n = 1;
        for (const auto& m : gRegex.matches) {
          if (n > 500) break;
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%d", n++);
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%zu-%zu", m.start, m.end);
          ImGui::TableSetColumnIndex(2);
          ImGui::TextUnformatted(m.text.c_str());
          for (int g = 3; g < cols; ++g) {
            ImGui::TableSetColumnIndex(g);
            if (static_cast<size_t>(g - 3) < m.groups.size()) ImGui::TextUnformatted(m.groups[g - 3].c_str());
          }
        }
        ImGui::EndTable();
      }
      if (!gRegex.replaced.empty()) {
        widgets::dimText("Replaced");
        theme::pushMono();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(gRegex.replaced.c_str());
        ImGui::PopTextWrapPos();
        theme::pop();
      }
    }
  } else if (gKind == playground::Kind::Sql) {
    if (!gSql.ok) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::TextWrapped("%s", gSql.error.c_str());
      ImGui::PopStyleColor();
    } else {
      theme::pushBold();
      size_t rows = 0;
      for (const auto& t : gSql.tables) rows += t.rows.size();
      ImGui::Text("%zu row%s%s in %.1f ms%s", rows, rows == 1 ? "" : "s", gSql.tables.size() > 1 ? (" over " + std::to_string(gSql.tables.size()) + " results").c_str() : "", gSql.ms,
                  gSql.changes ? (" - " + std::to_string(gSql.changes) + " row(s) changed").c_str() : "");
      theme::pop();
    }
    for (const std::string& line : gSql.schema) widgets::dimText("schema: %s", line.c_str());
    int tableIndex = 0;
    for (const playground::Table& t : gSql.tables) {
      ImGui::PushID(tableIndex++);
      const int cols = static_cast<int>(std::min<size_t>(t.columns.size(), 24));
      if (cols > 0 && ImGui::BeginTable("##pgtable", cols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        for (int c = 0; c < cols; ++c) ImGui::TableSetupColumn(t.columns[c].c_str());
        ImGui::TableHeadersRow();
        for (const auto& row : t.rows) {
          ImGui::TableNextRow();
          for (int c = 0; c < cols; ++c) {
            ImGui::TableSetColumnIndex(c);
            if (static_cast<size_t>(c) < row.size()) ImGui::TextUnformatted(row[c].c_str());
          }
        }
        ImGui::EndTable();
      }
      if (t.truncated) widgets::dimText("Only the first %zu rows are shown.", t.rows.size());
      if (t.columns.size() > 24) widgets::dimText("Only the first 24 of %zu columns are shown.", t.columns.size());
      ImGui::PopID();
    }
  } else {
    if (gRunning) widgets::dimText("Running jq...");
    theme::pushMono();
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(gOutput.c_str());
    ImGui::PopTextWrapPos();
    theme::pop();
  }
  ImGui::EndChild();

  // Footer.
  widgets::beginDisabled(!gRan || gRunning);
  if (ImGui::Button("Copy result")) {
    platform::setClipboardText(resultText(false));
    widgets::toast(widgets::ToastLevel::Success, "Copied the result");
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy as Markdown")) {
    platform::setClipboardText(resultText(true));
    widgets::toast(widgets::ToastLevel::Success, "Copied as Markdown");
  }
  ImGui::SameLine();
  if (ImGui::Button("Ask about the result")) {
    std::string result = resultText(true);
    if (result.size() > 6000) result = result.substr(0, 6000) + "\n...";
    composer = std::string("I ran this ") + playground::kindName(gKind) + " locally:\n\n```" + (gKind == playground::Kind::Sql ? "sql" : gKind == playground::Kind::Jq ? "jq" : "regex") + "\n" + gCode +
               "\n```\n\nResult:\n\n" + (gKind == playground::Kind::Sql ? result : "```\n" + result + "\n```") + "\n";
    ++composerGeneration;
    focusComposer = true;
    showPlayground = false;
    ImGui::CloseCurrentPopup();
  }
  widgets::endDisabled(!gRan || gRunning);
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    showPlayground = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace app
