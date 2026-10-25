// `Open WebUI --lsptest`: checks of the LSP client and the context helpers,
// with a live clangd round trip when clangd is installed.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/lsp.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string names(const std::vector<lsp::Term>& t) {
  std::string out;
  for (const auto& x : t) out += (x.qualifier.empty() ? "" : x.qualifier + "::") + x.name + "|";
  return out;
}
}  // namespace

int runLspSelfTest() {
  using namespace lsp;
  printf("Open WebUI desktop LSP-context self-test\n\n");

  Framer f;
  const std::string two = frame(Json{{"id", 1}, {"result", "a"}}) + frame(Json{{"id", 2}, {"result", Json::array()}});
  f.feed(two.data(), 10);
  Json m;
  const bool none = !f.next(&m);
  f.feed(two.data() + 10, two.size() - 10);
  const bool first = f.next(&m) && m["id"] == 1;
  const bool second = f.next(&m) && m["id"] == 2;
  report("framing: Content-Length frames split across reads and back to back", none && first && second && !f.next(&m) && frame(Json{{"a", 1}}).rfind("Content-Length: 7\r\n\r\n", 0) == 0);

  const auto terms = termsFromQuestion("How does `pool::order` pick a member, what does runPoolSelfTest() do, and is buildCompletionPayload or local_index slow? Also App.frame and the Tools pane.");
  report("termsFromQuestion: backticks, a::b, f(), camelCase, snake_case, a.b; plain words ignored", names(terms) == "pool::order|runPoolSelfTest|buildCompletionPayload|local_index|App::frame|", names(terms));
  report("termsFromQuestion: caps the count, ignores prose, short calls and e.g. / sentence ends", termsFromQuestion("What is the weather? Is it ok() to go, e.g. today? Fine. Then /no_think and src/my_file.cpp").empty() && termsFromQuestion("a_b c_d e_f g_h i_j k_l m_n", 3).size() == 3);

  const Json docSymbols = Json::parse(R"js([{"name":"pool","kind":3,"range":{"start":{"line":0,"character":0},"end":{"line":40,"character":1}},"selectionRange":{"start":{"line":0,"character":10},"end":{"line":0,"character":14}},
    "children":[{"name":"order","kind":12,"detail":"std::vector<std::string> (const Prefs &)","range":{"start":{"line":10,"character":0},"end":{"line":24,"character":1}},"selectionRange":{"start":{"line":10,"character":25},"end":{"line":10,"character":30}}},
                {"name":"Prefs","kind":23,"range":{"start":{"line":2,"character":0},"end":{"line":6,"character":2}},"selectionRange":{"start":{"line":2,"character":7},"end":{"line":2,"character":12}}}]},
    {"name":"pool::pick","kind":12,"range":{"start":{"line":42,"character":0},"end":{"line":45,"character":1}},"selectionRange":{"start":{"line":42,"character":18},"end":{"line":42,"character":22}}}])js");
  const auto flat = flattenDocumentSymbols(docSymbols, "/p/pool.cpp");
  report("flattenDocumentSymbols: nesting gives the container, ranges kept, out-of-line qualifiers split", flat.size() == 4 && flat[1].name == "order" && flat[1].container == "pool" && flat[1].line == 10 && flat[1].character == 25 &&
                                                                                                                flat[1].endLine == 24 && flat[3].name == "pick" && flat[3].container == "pool" && flat[1].path == "/p/pool.cpp");
  const Json wsSymbols = Json::parse(R"js([{"name":"order","kind":12,"containerName":"pool","location":{"uri":"file:///p/my%20dir/pool.h","range":{"start":{"line":30,"character":25},"end":{"line":30,"character":40}}}}])js");
  const auto ws = fromWorkspaceSymbols(wsSymbols);
  report("fromWorkspaceSymbols: SymbolInformation with containerName and a percent-encoded URI", ws.size() == 1 && ws[0].container == "pool" && ws[0].path == "/p/my dir/pool.h" && ws[0].line == 30);
  std::vector<Symbol> candidates = flat;
  candidates.push_back(ws[0]);
  const int best = bestMatch(candidates, Term{"order", "pool"});
  report("matches / bestMatch: qualifier respected, the definition with a body wins over the declaration", best == 1 && !matches(flat[1], Term{"order", "geo"}) && matches(ws[0], Term{"order", ""}) && bestMatch(flat, Term{"nothing", ""}) == -1);

  report("hoverText: MarkupContent, MarkedString objects, arrays, a cap",
         hoverText(Json{{"contents", Json{{"kind", "markdown"}, {"value", "### function `order`\n---\n→ `std::vector<std::string>`"}}}}).find("function `order`") != std::string::npos &&
             hoverText(Json{{"contents", Json::array({"plain", Json{{"language", "cpp"}, {"value", "int f()"}}})}}) == "plain\n```cpp\nint f()\n```" && hoverText(Json{{"contents", std::string(2000, 'x')}}, 100).size() == 103 &&
             hoverText(Json()).empty());
  report("snippet: inclusive lines, capped with a note", snippet("a\nb\nc\nd\ne\n", 1, 3) == "b\nc\nd\n" && snippet("a\nb\nc\nd\ne\n", 0, 4, 2) == "a\nb\n    // ... 3 more lines\n" && snippet("a\n", 5, 6).empty());
  report("uri round trip, languageIdFor, serverFor, defaults", uriToPath(pathToUri("/tmp/a b/c#d.cpp")) == "/tmp/a b/c#d.cpp" && pathToUri("/a b").find("%20") != std::string::npos && languageIdFor("x.mm") == "objective-cpp" &&
                                                                   languageIdFor("x.py") == "python" && serverFor(defaultServers(), "/a/b.hpp")->name == "clangd" && serverFor(defaultServers(), "/a/b.txt") == nullptr);
  Entry e;
  e.symbol = flat[1];
  e.symbol.path = "/root/src/pool.cpp";
  e.hover = "function order";
  e.code = "int x;\n";
  e.language = "cpp";
  const std::string block = formatBlock({e, e}, "/root");
  report("formatBlock: header, qualified name, kind, relative path:line, hover, fenced code; budget", block.find("### pool::order (function) - src/pool.cpp:11\nfunction order\n```cpp\nint x;\n```") != std::string::npos &&
                                                                                                          formatBlock({e, e}, "/root", 260).find("left out") != std::string::npos && formatBlock({}, "/root").empty());

  // Live: clangd on a two-file temp project.
  const std::string clangd = findExecutable("clangd");
  if (clangd.empty()) {
    report("live clangd round trip (skipped: clangd not installed)", true);
  } else {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "owui-lsptest";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "geo.h") << "#pragma once\nnamespace geo {\n/// Area of a w-by-h rectangle.\nint area(int w, int h);\nstruct Box { int w = 0, h = 0; };\n}\n";
    std::ofstream(dir / "geo.cpp") << "#include \"geo.h\"\nnamespace geo {\nint area(int w, int h) {\n  return w * h;\n}\n}  // namespace geo\n";
    Client c;
    std::string err;
    const bool started = c.start(defaultServers()[0], dir.string(), &err);
    const std::string cpp = (dir / "geo.cpp").string();
    const bool opened = started && c.openFile(cpp, &err);
    const Json syms = opened ? c.request("textDocument/documentSymbol", Json{{"textDocument", Json{{"uri", pathToUri(cpp)}}}}, 15000, &err) : Json();
    const auto list = flattenDocumentSymbols(syms, cpp);
    const int at = bestMatch(list, Term{"area", "geo"});
    std::string hover;
    if (at >= 0) {
      const Json h = c.request("textDocument/hover", Json{{"textDocument", Json{{"uri", pathToUri(cpp)}}}, {"position", Json{{"line", list[at].line}, {"character", list[at].character}}}}, 15000, &err);
      hover = hoverText(h);
    }
    report("live clangd: initialize, didOpen, documentSymbol finds geo::area, hover shows its signature and doc comment",
           started && at >= 0 && list[at].endLine == 4 && hover.find("int area(int w, int h)") != std::string::npos && hover.find("Area of a w-by-h rectangle") != std::string::npos, err.empty() ? hover.substr(0, 80) : err);
    c.stop();
    report("live clangd: shut down cleanly", !c.alive());
    std::filesystem::remove_all(dir);
  }

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
