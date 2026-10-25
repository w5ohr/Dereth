// `Open WebUI --browsertest`: offline checks of the built-in browser tool
// (URL policy, result clipping, search-page parsing, tool specs). No network.
#include <cstdio>
#include <string>

#include "browser_tool.h"
#include "html_markdown.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runBrowserSelfTest() {
  using namespace browsertool;
  printf("Open WebUI desktop browser-tool self-test\n\n");

  report("normalizeUrl adds https, strips quotes/brackets",
         normalizeUrl(" example.com/a ") == "https://example.com/a" && normalizeUrl("<https://x.org>") == "https://x.org" &&
             normalizeUrl("\"http://y.net\"") == "http://y.net");
  report("checkUrl: only http(s)", !checkUrl("file:///etc/passwd", true).empty() && !checkUrl("ftp://x", true).empty() &&
                                       checkUrl("https://example.com", false).empty());
  report("checkUrl: private hosts blocked unless allowed",
         !checkUrl("http://localhost:3001/x", false).empty() && !checkUrl("http://127.0.0.1/", false).empty() &&
             !checkUrl("http://10.1.2.3/", false).empty() && !checkUrl("http://192.168.1.5:8080/", false).empty() &&
             !checkUrl("http://172.20.0.1/", false).empty() && !checkUrl("http://169.254.169.254/latest/meta-data", false).empty() &&
             !checkUrl("http://[::1]/", false).empty() && !checkUrl("http://printer.local/", false).empty() &&
             !checkUrl("http://100.85.156.44/", false).empty() && checkUrl("http://localhost:3001/x", true).empty() &&
             checkUrl("http://172.32.0.1/", false).empty() && checkUrl("https://user:pw@example.org/", false).empty());
  report("clip adds a truncation note", clip("abcdef", 10) == "abcdef" && clip(std::string(100, 'x'), 20).find("[... truncated: 80 more characters]") != std::string::npos);
  report("percentDecode", percentDecode("https%3A%2F%2Fexample.com%2Fa+b%20c") == "https://example.com/a b c");

  const std::string ddg =
      "<div class=\"result results_links\"><h2 class=\"result__title\">"
      "<a rel=\"nofollow\" class=\"result__a\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fopenwebui.com%2F&amp;rut=abc\">Open WebUI <b>home</b></a></h2>"
      "<a class=\"result__snippet\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fopenwebui.com%2F\">User-friendly <b>AI</b> interface &amp; more</a></div>"
      "<div class=\"result\"><a class=\"result__a\" href=\"https://github.com/open-webui/open-webui\">GitHub repo</a>"
      "<a class=\"result__snippet\">Source code.</a></div>"
      "<div class=\"result result--ad\"><a class=\"result__a\" href=\"//duckduckgo.com/y.js?ad\">Ad</a></div>";
  std::vector<SearchHit> hits = parseDuckDuckGo(ddg);
  report("parseDuckDuckGo: titles, decoded uddg URLs, snippets; non-http targets dropped",
         hits.size() == 2 && hits[0].title == "Open WebUI home" && hits[0].url == "https://openwebui.com/" &&
             hits[0].snippet == "User-friendly AI interface & more" && hits[1].url == "https://github.com/open-webui/open-webui" &&
             hits[1].snippet == "Source code.",
         hits.empty() ? "no hits" : hits[0].title + " | " + hits[0].url + " | " + hits[0].snippet);

  const std::vector<agents::ToolSpec> s = specs();
  bool names = s.size() == 5;
  for (const agents::ToolSpec& spec : s) names = names && spec.name.rfind("browser__", 0) == 0 && spec.serverId == kServerId && spec.parameters["type"] == "object";
  report("specs: five browser__ tools under the builtin server with object schemas", names);

  Options o;
  std::string error;
  std::string out = call("fetch", Json{{"url", "http://localhost:3001/"}}, o, &error);
  report("call: blocked URL is an error, no request made", out.empty() && error.find("blocked") != std::string::npos);
  error.clear();
  out = call("fetch", Json{{"url", "mailto:a@b.c"}}, o, &error);
  report("call: non-http scheme refused", out.empty() && !error.empty());
  error.clear();
  out = call("search", Json::object(), o, &error);
  report("call: search without a query", out.empty() && !error.empty());
  error.clear();
  out = call("teleport", Json{{"url", "https://example.com"}}, o, &error);
  report("call: unknown tool", out.empty() && error.find("unknown") != std::string::npos);

  std::string title;
  const std::string md = app::htmlToMarkdownText("<html><head><title>Hi there</title></head><body><h1>Head</h1><p>Para with <a href=\"/x\">link</a>.</p></body></html>",
                                                 "https://example.com/base/", &title);
  report("htmlToMarkdown wrapper: title, heading, resolved link", title == "Hi there" && md.find("Head") != std::string::npos &&
                                                                       md.find("https://example.com/x") != std::string::npos, md.substr(0, 120));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
