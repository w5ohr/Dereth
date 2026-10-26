// `Open WebUI --editortest`: offline checks of the editor bridge.
#include <cstdio>
#include <string>

#include "core/editorbridge.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runEditorSelfTest() {
  using namespace editorbridge;
  printf("Open WebUI desktop editor-bridge self-test\n\n");

  FileRef r;
  report("parseFileRef: relative path with line", parseFileRef("src/app.cpp:120", &r) && r.path == "src/app.cpp" && r.line == 120 && r.column == 0);
  report("parseFileRef: absolute path with line and column", parseFileRef("/Users/x/a.h:3:4", &r) && r.path == "/Users/x/a.h" && r.line == 3 && r.column == 4);
  report("parseFileRef: home path, bare file with extension", parseFileRef("~/x.py:9", &r) && r.path == "~/x.py" && r.line == 9 && parseFileRef("Makefile.in:2", &r));
  report("parseFileRef rejects times, URLs, words, zero lines, dirs",
         !parseFileRef("12:30", &r) && !parseFileRef("http://x:80", &r) && !parseFileRef("foo:12", &r) && !parseFileRef("a.c:0", &r) && !parseFileRef("src/:3", &r) &&
             !parseFileRef("a.c", &r) && !parseFileRef("note: 12", &r));
  parseFileRef("src/app.cpp:120:7", &r);
  report("linkFor / parseLink round trip", linkFor(r) == "editor://src/app.cpp:120:7" && [&] { FileRef b; return parseLink(linkFor(r), &b) && b.path == r.path && b.line == 120 && b.column == 7; }() &&
                                                !parseLink("https://x", &r));
  report("resolvePath", resolvePath("~/a.c", "/proj", "/Users/me") == "/Users/me/a.c" && resolvePath("/abs/a.c", "/proj", "/Users/me") == "/abs/a.c" &&
                            resolvePath("src/a.c", "/proj/", "/Users/me") == "/proj/src/a.c" && resolvePath("src/a.c", "", "/Users/me") == "src/a.c");

  FileRef abs{"/proj/src/app.cpp", 120, 7};
  report("openCommand: VS Code / Cursor / Zed URL schemes with line and column",
         openCommand(Editor::VSCode, abs, "") == std::vector<std::string>{"/usr/bin/open", "vscode://file/proj/src/app.cpp:120:7"} &&
             openCommand(Editor::Cursor, abs, "")[1] == "cursor://file/proj/src/app.cpp:120:7" && openCommand(Editor::Zed, abs, "")[1] == "zed://file/proj/src/app.cpp:120:7");
  report("openCommand: Xcode and Sublime", openCommand(Editor::Xcode, abs, "") == std::vector<std::string>{"/usr/bin/xed", "-l", "120", "/proj/src/app.cpp"} &&
                                              openCommand(Editor::Sublime, abs, "") == std::vector<std::string>{"subl", "/proj/src/app.cpp:120:7"});
  FileRef spaced{"/My Projects/a b.c", 5, 0};
  std::vector<std::string> custom = openCommand(Editor::Custom, spaced, "/usr/local/bin/myedit --goto {file} {line} {col}");
  report("openCommand: custom template keeps a path with spaces as one argument, column defaults to 1",
         custom == std::vector<std::string>{"/usr/local/bin/myedit", "--goto", "/My Projects/a b.c", "5", "1"});
  report("splitArgs quotes", splitArgs("a 'b c' \"d e\" f") == std::vector<std::string>{"a", "b c", "d e", "f"});
  report("editorFrom / editorId / editorName", editorFrom("Zed") == Editor::Zed && editorId(Editor::Xcode) == "xcode" && editorName(Editor::Cursor) == "Cursor" && editorFrom("?") == Editor::VSCode);
  const std::string prompt = explainPrompt("src/app.cpp", 12, "cpp", "int x = 1;");
  report("explainPrompt", prompt.find("from src/app.cpp:12") != std::string::npos && prompt.find("```cpp\nint x = 1;\n```") != std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
