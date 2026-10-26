// `Open WebUI --applytest`: offline checks of the file-block extractor.
#include <cstdio>
#include <string>

#include "core/fileblocks.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string paths(const std::vector<fileblocks::Block>& b) {
  std::string s;
  for (const auto& x : b) s += x.path + "(" + x.source + ") ";
  return s;
}
}  // namespace

int runFileBlocksSelfTest() {
  using namespace fileblocks;
  printf("Open WebUI desktop file-blocks self-test\n\n");

  auto b = extract("Here you go.\n\n```python path=src/app.py\nprint(1)\n```\n\n```ts title=\"web/x.ts\"\nlet a = 1\n```\n");
  report("info string path= and title=", b.size() == 2 && b[0].path == "src/app.py" && b[0].language == "python" &&
                                             b[1].path == "web/x.ts" && b[1].source == "info", paths(b));
  b = extract("```js src/main.js\nconsole.log(1)\n```");
  report("bare path token in info", b.size() == 1 && b[0].path == "src/main.js" && b[0].language == "js", paths(b));
  b = extract("```python\n# file: tools/run.py\nimport os\n\nprint(os.getcwd())\n```");
  report("first-line comment, stripped from the body", b.size() == 1 && b[0].path == "tools/run.py" &&
                                                          b[0].source == "comment" && b[0].code == "import os\n\nprint(os.getcwd())\n", paths(b) + "|" + (b.empty() ? "" : b[0].code));
  b = extract("```html\n<!-- index.html -->\n<p>hi</p>\n```");
  report("html comment path", b.size() == 1 && b[0].path == "index.html", paths(b));
  b = extract("### src/lib/util.py\n\n```python\nx = 1\n```\n\n**tests/test_util.py**\n```python\nassert True\n```\n\n`Makefile`:\n```make\nall:\n```");
  report("heading, bold and inline-code paths above fences", b.size() == 3 && b[0].path == "src/lib/util.py" &&
                                                                 b[1].path == "tests/test_util.py" && b[2].path == "Makefile" && b[2].source == "heading", paths(b));
  b = extract("Some prose about version 3.11 and then:\n```python\nprint(1)\n```");
  report("a version number above is not a path", b.empty(), paths(b));
  b = extract("```python\nprint(1)\n```\nThis block has no name.");
  report("unnamed block ignored", b.empty(), paths(b));
  b = extract("```diff\n--- a/x.py\n+++ b/x.py\n@@ -1 +1 @@\n-a\n+b\n```");
  report("diff blocks left to the diff path", b.empty(), paths(b));
  b = extract("```python path=a.py\nv1\n```\nlater:\n```python path=a.py\nv2\n```");
  report("later block for the same path wins", b.size() == 1 && b[0].code == "v2\n", paths(b));
  b = extract("```python path=a.py\nunterminated");
  report("unclosed fence ignored", b.empty());
  b = extract("Update `src/app.py`:\n```python\nprint(2)\n```");
  report("'Update `path`:' label", b.size() == 1 && b[0].path == "src/app.py", paths(b));
  report("looksLikePath", looksLikePath("src/a.py") && looksLikePath("Makefile/x") && looksLikePath("a.txt") &&
                              !looksLikePath("hello") && !looksLikePath("3.11") && !looksLikePath("http://x.y/z") &&
                              !looksLikePath("foo(bar).py"));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
