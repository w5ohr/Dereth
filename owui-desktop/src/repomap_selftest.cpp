// `Open WebUI --repomaptest`: offline checks of the symbol extractors and
// the map renderer.
#include <cstdio>
#include <string>

#include "core/repomap.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string names(const std::vector<repomap::Symbol>& s) {
  std::string out;
  for (const auto& x : s) out += x.kind + ":" + x.name + " ";
  return out;
}
}  // namespace

int runRepoMapSelfTest() {
  using namespace repomap;
  printf("Open WebUI desktop repo-map self-test\n\n");

  auto s = extract("python", "import os\nMAX_RETRIES = 3\n\nclass Loader:\n    def __init__(self, path):\n        pass\n\n    async def load(self):\n        pass\n\ndef helper(x):\n    return x\n# def not_a_func():\n");
  report("python: class, methods, func, const", names(s) == "const:MAX_RETRIES class:Loader method:__init__ method:load func:helper ", names(s));
  report("python: line numbers", s.size() == 5 && s[1].line == 4 && s[4].line == 11);

  s = extract("typescript", "import x from 'y';\nexport interface Props { a: number }\nexport default class Widget extends Base {\n  render(): string {\n    return '';\n  }\n  private tick() {\n  }\n}\nexport const useThing = (a: number) => a;\nfunction plain() {}\nexport const VALUE: number = 3;\n");
  report("typescript: interface, class, methods, arrow func, function, export",
         names(s) == "type:Props class:Widget method:render method:tick func:useThing func:plain export:VALUE ", names(s));

  s = extract("cpp", "#include <x>\nnamespace app {\nstruct Entry {\n  int a;\n};\nclass App : public Base {\n};\nstatic std::string helper(const std::string& s) {\n  return s;\n}\nvoid App::run(int argc, char** argv) {\n  if (argc > 1) {\n  }\n}\nusing Json = nlohmann::json;\nconstexpr int kLimit = 3;\n}  // namespace app\n");
  report("cpp: namespace, struct, class, funcs, using, constexpr",
         names(s) == "section:app class:Entry class:App func:helper func:App::run type:Json const:kLimit ", names(s));

  s = extract("go", "package main\n\ntype Server struct{}\n\nfunc (s *Server) Start() error { return nil }\nfunc main() {}\n");
  report("go: type, method receiver, func", names(s) == "type:Server func:Start func:main ", names(s));

  s = extract("rust", "pub struct Config {}\nimpl Config {\n    pub fn new() -> Self { Self {} }\n}\nfn private_helper() {}\nmod tests {}\n");
  report("rust: struct, impl, fns, mod", names(s) == "type:Config section:Config func:new func:private_helper section:tests ", names(s));

  s = extract("markdown", "# Title\nText\n## Install\n```\n# not a heading\n```\n### Notes\n#### too deep\n");
  report("markdown: headings to depth 3, fences skipped", names(s) == "section:Title section:Install section:Notes ", names(s));

  report("languageOf", languageOf("a/b.py") == "python" && languageOf("x.tsx") == "typescript" && languageOf("h.hpp") == "cpp" && languageOf("file.txt").empty());

  FileMap a{"src/a.py", "python", extract("python", "def one():\n    pass\ndef two():\n    pass\n")};
  FileMap b{"src/b.py", "python", extract("python", "def only():\n    pass\n")};
  FileMap c{"src/c.txt", "", {}};
  const std::string full = render({a, b, c}, 10000);
  report("render: one line per file, name Lline, empty files skipped",
         full.find("src/a.py: one() L1, two() L3\n") != std::string::npos && full.find("src/b.py: only() L1\n") != std::string::npos && full.find("c.txt") == std::string::npos, full);
  FileMap big{"src/big.py", "python", {}};
  for (int i = 1; i <= 50; ++i) big.symbols.push_back({"method", "m" + std::to_string(i), "", i});
  big.symbols.push_back({"func", "keep", "", 99});
  const std::string condensed = render({big, a}, 400);
  report("render: over budget drops methods before files",
         condensed.find("src/big.py: keep() L99") != std::string::npos && condensed.find("m1()") == std::string::npos && condensed.find("src/a.py") != std::string::npos && condensed.find("condensed") != std::string::npos, condensed);
  const std::string tiny = render({big, a}, 140);
  report("render: files dropped only as a last resort, with a note", tiny.find("more file") != std::string::npos, tiny);

  {
    // A minified line and a backtracking-hostile one must not throw (they did: error_complexity).
    std::string minified = "export const a = (" + std::string(120000, 'x') + ") => 1;";
    std::string hostile = "const z: " + std::string(1500, '=') + " = y;";
    bool threw = false;
    try {
      repomap::extract("typescript", minified + "\nconst ok = () => 2;\n" + hostile + "\n");
    } catch (...) {
      threw = true;
    }
    report("pathological lines are skipped, never thrown", !threw);
  }
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
