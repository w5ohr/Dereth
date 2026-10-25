// `Open WebUI --fileeditortest`: offline checks of the file editor's core.
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/common.h"
#include "core/fileeditor.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runFileEditorSelfTest() {
  using namespace fileeditor;
  printf("Open WebUI desktop file-editor self-test\n\n");

  const std::string dir = (std::filesystem::temp_directory_path() / "owui-editortest").string();
  std::filesystem::remove_all(dir);
  const std::string path = "/tmp/some dir/hello.py";
  report("historyFile: stable, path-safe name", historyFile(dir, path) == historyFile(dir, path) && historyFile(dir, path).find("some dir") == std::string::npos && historyFile(dir, path).size() > dir.size() + 20);
  report("loadHistory: empty when nothing recorded", loadHistory(dir, path).empty());
  report("record creates the folder and the first snapshot", record(dir, path, "a = 1\n", "before apply") && loadHistory(dir, path).size() == 1 && loadHistory(dir, path)[0].label == "before apply");
  record(dir, path, "a = 1\n", "duplicate");
  report("record skips a snapshot equal to the newest", loadHistory(dir, path).size() == 1);
  record(dir, path, "a = 2\n", "apply");
  record(dir, path, "a = 3\n", "saved in the editor");
  const auto h = loadHistory(dir, path);
  report("snapshots keep their order and content", h.size() == 3 && h[1].label == "apply" && h[2].content == "a = 3\n" && h[0].at > 0);
  for (int i = 0; i < 40; ++i) record(dir, path, "v" + std::to_string(i) + "\n", "spam", 5);
  report("record keeps the newest `keep`", loadHistory(dir, path).size() == 5 && loadHistory(dir, path).back().content == "v39\n");
  std::filesystem::remove_all(dir);

  Snapshot s{core::nowSeconds() - 130, "apply", "x\ny\nz"};
  report("describe", describe(s, core::nowSeconds()) == "apply - 3 lines - 2 min ago" && describe(Snapshot{core::nowSeconds(), "s", ""}, core::nowSeconds()) == "s - 0 lines - just now");

  report("languageFor", languageFor("a/b.py") == "python" && languageFor("x.tsx") == "typescript" && languageFor("src/app.mm") == "cpp" && languageFor("Makefile") == "bash" && languageFor("notes.txt").empty() &&
                            languageFor("/dir.with.dots/file") .empty() && languageFor("README.md") == "markdown");
  const std::string text = "one\ntwo\nthree\n";
  report("lineCount / lineOf / offsetOfLine", lineCount(text) == 3 && lineCount("") == 0 && lineCount("a") == 1 && lineOf(text, 0) == 1 && lineOf(text, 5) == 2 && lineOf(text, 99) == 4 && offsetOfLine(text, 2) == 4 &&
                                                   offsetOfLine(text, 1) == 0 && offsetOfLine(text, 50) == text.size());
  report("findNext: forward, wraps once, caseless, absent", findNext(text, "t", 0, false) == 4 && findNext(text, "t", 5, false) == 8 && findNext(text, "t", 9, false) == 4 && findNext(text, "TWO", 0, true) == 4 &&
                                                                 findNext(text, "TWO", 0, false) == std::string::npos && findNext(text, "", 0, false) == std::string::npos);
  report("firstDifferingLine", firstDifferingLine("a\nb\nc", "a\nb\nc") == 0 && firstDifferingLine("a\nb\nc", "a\nx\nc") == 2 && firstDifferingLine("a\nb", "a\nb\nc") == 3);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
