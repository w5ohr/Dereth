#include "browser_tool.h"

#include <algorithm>
#include <atomic>
#include <cctype>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/doctext.h"
#include "core/http.h"
#include "core/settings.h"
#include "html_markdown.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace browsertool {
namespace {

constexpr size_t kMaxBody = 8 * 1024 * 1024;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string hostOf(const std::string& url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return "";
  std::string rest = url.substr(scheme + 3);
  const size_t end = rest.find_first_of("/?#");
  if (end != std::string::npos) rest = rest.substr(0, end);
  const size_t at = rest.rfind('@');
  if (at != std::string::npos) rest = rest.substr(at + 1);
  if (!rest.empty() && rest[0] == '[') {   // [::1]:port
    const size_t close = rest.find(']');
    return lower(rest.substr(1, close == std::string::npos ? std::string::npos : close - 1));
  }
  const size_t colon = rest.find(':');
  return lower(colon == std::string::npos ? rest : rest.substr(0, colon));
}

bool privateHost(const std::string& host) {
  if (host.empty() || host == "localhost" || host == "0.0.0.0" || host == "::1" || host == "::") return true;
  if (core::endsWith(host, ".local") || core::endsWith(host, ".localhost") || core::endsWith(host, ".internal")) return true;
  if (core::startsWith(host, "fe80:") || core::startsWith(host, "fc") || core::startsWith(host, "fd")) return true;
  int a = -1, b = -1, c = -1, d = -1;
  if (sscanf(host.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
    if (a == 10 || a == 127 || a == 0) return true;
    if (a == 169 && b == 254) return true;
    if (a == 172 && b >= 16 && b <= 31) return true;
    if (a == 192 && b == 168) return true;
    if (a == 100 && b >= 64 && b <= 127) return true;   // CGNAT / Tailscale
  }
  return false;
}

std::string stripTags(const std::string& html) {
  std::string out;
  bool inTag = false;
  for (char c : html) {
    if (c == '<') inTag = true;
    else if (c == '>') inTag = false;
    else if (!inTag) out += c;
  }
  return core::trim(out);
}

std::string decodeEntities(const std::string& s) {
  std::string out = s;
  const std::pair<const char*, const char*> table[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#x27;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
  for (const auto& [from, to] : table) {
    size_t at;
    while ((at = out.find(from)) != std::string::npos) out.replace(at, strlen(from), to);
  }
  return out;
}

std::string attrOf(const std::string& tag, const std::string& name) {
  const size_t at = tag.find(name + "=\"");
  if (at == std::string::npos) return "";
  const size_t start = at + name.size() + 2;
  const size_t end = tag.find('"', start);
  return end == std::string::npos ? "" : tag.substr(start, end - start);
}

struct Fetched {
  long status = 0;
  std::string body, contentType, finalUrl, error;
};

Fetched fetch(const std::string& url, int timeoutSec, bool allowLocal) {
  Fetched out;
  out.finalUrl = url;
  const std::string why = checkUrl(url, allowLocal);
  if (!why.empty()) {
    out.error = why;
    return out;
  }
  net::Request req;
  req.url = url;
  req.timeoutSec = timeoutSec;
  req.connectTimeoutSec = std::min(10, timeoutSec);
  req.headers.push_back({"Accept", "text/html,application/xhtml+xml,application/pdf,text/*;q=0.9,*/*;q=0.5"});
  req.headers.push_back({"Accept-Language", "en"});
  std::string body;
  bool tooBig = false;
  net::Response res = net::stream(req, [&](const char* data, size_t len) {
    body.append(data, len);
    if (body.size() > kMaxBody) {
      tooBig = true;
      return false;
    }
    return true;
  });
  out.status = res.status;
  out.body = body;
  out.contentType = lower(res.header("content-type"));
  const std::string location = res.header("location");
  if (!res.error.empty() && !tooBig) {
    out.error = res.error;
  } else if (res.status >= 400) {
    out.error = "HTTP " + std::to_string(res.status);
  } else if (tooBig) {
    out.error = "The page is larger than 8 MB";
  }
  return out;
}

// What the model gets for a fetched body: Markdown for HTML, pages for PDF,
// the text itself otherwise.
std::string bodyText(const Fetched& f, const std::string& url, std::string* title) {
  const std::string type = f.contentType;
  if (type.find("text/html") != std::string::npos || type.find("xhtml") != std::string::npos ||
      (type.empty() && f.body.find('<') != std::string::npos)) {
    return app::htmlToMarkdownText(f.body, url, title);
  }
  if (type.find("application/pdf") != std::string::npos || core::endsWith(lower(url), ".pdf")) {
    const doctext::Document doc = doctext::extract("page.pdf", "application/pdf", f.body);
    if (!doc.ok()) return "(PDF: " + doc.error + ")";
    std::string out;
    for (const doctext::Page& p : doc.pages) out += "[" + p.label + "] " + p.text + "\n";
    return out;
  }
  if (type.find("json") != std::string::npos || type.find("text/") != std::string::npos || type.find("xml") != std::string::npos ||
      type.find("javascript") != std::string::npos || type.empty()) {
    if (f.body.find('\0') != std::string::npos) return "(binary content: " + type + ")";
    return f.body;
  }
  return "(binary content: " + type + ", " + core::humanBytes(static_cast<int64_t>(f.body.size())) + " - not readable as text)";
}

std::string head(const std::string& title, const std::string& url) {
  std::string out;
  if (!title.empty()) out += "Title: " + title + "\n";
  out += "URL: " + url + "\n\n";
  return out;
}

Json schema(std::initializer_list<std::pair<const char*, Json>> props, std::initializer_list<const char*> required) {
  Json properties = Json::object();
  for (const auto& [name, def] : props) properties[name] = def;
  Json out{{"type", "object"}, {"properties", properties}};
  Json req = Json::array();
  for (const char* r : required) req.push_back(r);
  if (!req.empty()) out["required"] = req;
  return out;
}

}  // namespace

Options optionsFromSettings() {
  const Json& extra = core::Settings::instance().extra;
  Options o;
  o.enabled = extra.value("browserTool", true);
  o.allowLocal = extra.value("browserToolAllowLocal", false);
  o.maxChars = static_cast<size_t>(std::clamp(extra.value("browserToolMaxChars", 12000), 2000, 60000));
  o.timeoutSec = std::clamp(extra.value("browserToolTimeout", 20), 5, 90);
  return o;
}

std::vector<agents::ToolSpec> specs() {
  std::vector<agents::ToolSpec> out;
  auto add = [&](const char* name, const char* description, Json parameters) {
    agents::ToolSpec s;
    s.name = std::string(kServerName) + "__" + name;
    s.description = description;
    s.parameters = std::move(parameters);
    s.serverId = kServerId;
    s.serverName = kServerName;
    s.tool = name;
    out.push_back(s);
  };
  const Json urlProp{{"type", "string"}, {"description", "Absolute http(s) URL"}};
  add("fetch", "Download a web page (or PDF, JSON, text file) and return its readable content as Markdown with the title and final URL. Fast; no JavaScript runs. Use this first.",
      schema({{"url", urlProp}}, {"url"}));
  add("render", "Load a page in a real browser engine so its JavaScript runs, then return the visible text and up to 150 links. Slower than fetch; use it when fetch returns an empty or script-only page.",
      schema({{"url", urlProp}, {"wait_ms", Json{{"type", "integer"}, {"description", "Extra time to let scripts settle after load (0-10000, default 800)"}}}}, {"url"}));
  add("links", "List the links on a page (text and URL), rendered with JavaScript. Useful to find the next page to open.",
      schema({{"url", urlProp}}, {"url"}));
  add("screenshot", "Render a page and save a PNG screenshot of its first screen; returns the file path and size. The user can open the path.",
      schema({{"url", urlProp}, {"width", Json{{"type", "integer"}, {"description", "Viewport width in px (480-2000, default 1100)"}}},
              {"height", Json{{"type", "integer"}, {"description", "Viewport height in px (320-4000, default 800)"}}}}, {"url"}));
  add("search", "Search the web (DuckDuckGo) and return the top results as title, URL and snippet. Follow up with fetch on a result URL to read it.",
      schema({{"query", Json{{"type", "string"}, {"description", "Search terms"}}},
              {"max_results", Json{{"type", "integer"}, {"description", "1-10, default 6"}}}}, {"query"}));
  return out;
}

std::string normalizeUrl(const std::string& raw) {
  std::string url = core::trim(raw);
  if (url.size() >= 2 && ((url.front() == '"' && url.back() == '"') || (url.front() == '<' && url.back() == '>'))) {
    url = url.substr(1, url.size() - 2);
  }
  if (url.empty()) return url;
  if (url.find("://") == std::string::npos) url = "https://" + url;
  return url;
}

std::string checkUrl(const std::string& url, bool allowLocal) {
  const std::string l = lower(url);
  if (!(core::startsWith(l, "http://") || core::startsWith(l, "https://"))) return "Only http and https URLs are allowed";
  const std::string host = hostOf(url);
  if (host.empty()) return "The URL has no host";
  if (!allowLocal && privateHost(host)) {
    return "Local and private-network addresses are blocked (switch on \"allow local hosts\" under Tools > MCP > Browser tool to permit them)";
  }
  return "";
}

std::string clip(const std::string& text, size_t maxChars) {
  if (text.size() <= maxChars) return text;
  return text.substr(0, maxChars) + "\n\n[... truncated: " + std::to_string(text.size() - maxChars) + " more characters]";
}

std::string percentDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) && isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else if (s[i] == '+') {
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}

std::vector<SearchHit> parseDuckDuckGo(const std::string& html) {
  std::vector<SearchHit> out;
  size_t at = 0;
  while ((at = html.find("result__a", at)) != std::string::npos) {
    const size_t tagStart = html.rfind('<', at);
    const size_t tagEnd = html.find('>', at);
    if (tagStart == std::string::npos || tagEnd == std::string::npos) break;
    const std::string tag = html.substr(tagStart, tagEnd - tagStart + 1);
    const size_t close = html.find("</a>", tagEnd);
    if (close == std::string::npos) break;
    SearchHit hit;
    hit.title = decodeEntities(stripTags(html.substr(tagEnd + 1, close - tagEnd - 1)));
    std::string href = decodeEntities(attrOf(tag, "href"));
    const size_t uddg = href.find("uddg=");
    if (uddg != std::string::npos) {
      std::string target = href.substr(uddg + 5);
      const size_t amp = target.find('&');
      if (amp != std::string::npos) target = target.substr(0, amp);
      href = percentDecode(target);
    } else if (core::startsWith(href, "//")) {
      href = "https:" + href;
    }
    // Ads and DuckDuckGo's own links carry no uddg target: not results.
    if (href.find("duckduckgo.com/") != std::string::npos) {
      at = close;
      continue;
    }
    hit.url = href;
    const size_t snip = html.find("result__snippet", close);
    const size_t nextResult = html.find("result__a", close);
    if (snip != std::string::npos && (nextResult == std::string::npos || snip < nextResult)) {
      const size_t snipEnd = html.find('>', snip);
      const size_t snipClose = snipEnd == std::string::npos ? std::string::npos : html.find("</a>", snipEnd);
      if (snipEnd != std::string::npos && snipClose != std::string::npos) {
        hit.snippet = decodeEntities(stripTags(html.substr(snipEnd + 1, snipClose - snipEnd - 1)));
      }
    }
    if (!hit.title.empty() && core::startsWith(hit.url, "http")) out.push_back(hit);
    at = close;
  }
  return out;
}

std::string call(const std::string& tool, const Json& args, const Options& options, std::string* error) {
  auto arg = [&](const char* key) { return args.is_object() && args.contains(key) && args[key].is_string() ? args[key].get<std::string>() : ""; };
  auto num = [&](const char* key, int fallback) {
    if (!args.is_object() || !args.contains(key)) return fallback;
    if (args[key].is_number()) return args[key].get<int>();
    if (args[key].is_string()) return atoi(args[key].get<std::string>().c_str());
    return fallback;
  };

  if (tool == "search") {
    const std::string query = core::trim(arg("query"));
    if (query.empty()) {
      *error = "search needs a query";
      return "";
    }
    const int want = std::clamp(num("max_results", 6), 1, 10);
    std::string encoded;
    for (unsigned char c : query) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded += static_cast<char>(c);
      else if (c == ' ') encoded += '+';
      else {
        char buf[4];
        snprintf(buf, sizeof buf, "%%%02X", c);
        encoded += buf;
      }
    }
    const Fetched f = fetch("https://html.duckduckgo.com/html/?q=" + encoded, options.timeoutSec, true);
    if (!f.error.empty()) {
      *error = "search failed: " + f.error;
      return "";
    }
    const std::vector<SearchHit> hits = parseDuckDuckGo(f.body);
    if (hits.empty()) return "No results (or the search page changed its layout). Try different words, or fetch a likely URL directly.";
    std::string out = "Results for \"" + query + "\":\n\n";
    for (size_t i = 0; i < hits.size() && static_cast<int>(i) < want; ++i) {
      out += std::to_string(i + 1) + ". " + hits[i].title + "\n   " + hits[i].url + "\n";
      if (!hits[i].snippet.empty()) out += "   " + hits[i].snippet + "\n";
    }
    return clip(out, options.maxChars);
  }

  const std::string url = normalizeUrl(arg("url"));
  if (url.empty()) {
    *error = tool + " needs a url";
    return "";
  }
  const std::string why = checkUrl(url, options.allowLocal);
  if (!why.empty()) {
    *error = why;
    return "";
  }

  if (tool == "fetch") {
    const Fetched f = fetch(url, options.timeoutSec, options.allowLocal);
    if (!f.error.empty() && f.body.empty()) {
      *error = f.error;
      return "";
    }
    std::string title;
    std::string text = bodyText(f, url, &title);
    if (!f.error.empty()) text = "(" + f.error + ")\n\n" + text;
    if (core::trim(text).empty()) text = "(the page has no readable text - it may need JavaScript; try render)";
    return head(title, url) + clip(text, options.maxChars);
  }

  if (tool == "render" || tool == "links" || tool == "screenshot") {
    const int width = std::clamp(num("width", 1100), 480, 2000);
    const int height = std::clamp(num("height", 800), 320, 4000);
    const int settle = std::clamp(num("wait_ms", 800), 0, 10000);
    native::BrowseResult r = native::browseUrl(url, static_cast<float>(width), static_cast<float>(height), options.timeoutSec * 1000,
                                               settle, tool == "screenshot");
    if (!r.error.empty() && r.text.empty() && r.png.empty()) {
      *error = r.error;
      return "";
    }
    if (tool == "screenshot") {
      if (r.png.empty()) {
        *error = "no screenshot came back";
        return "";
      }
      static std::atomic<int> counter{0};
      const std::string path = core::cacheDir() + "/browser-" + std::to_string(static_cast<long>(core::nowSeconds())) + "-" + std::to_string(++counter) + ".png";
      if (!core::writeFile(path, r.png)) {
        *error = "could not write " + path;
        return "";
      }
      return head(r.title, r.url.empty() ? url : r.url) + "Screenshot saved: " + path + " (" + std::to_string(width) + "x" + std::to_string(height) +
             " viewport, " + core::humanBytes(static_cast<int64_t>(r.png.size())) + ")";
    }
    std::string out = head(r.title, r.url.empty() ? url : r.url);
    if (!r.error.empty()) out += "(" + r.error + ")\n\n";
    if (tool == "render") {
      out += core::trim(r.text).empty() ? "(no visible text)" : r.text;
      if (!r.links.empty()) {
        out += "\n\nLinks (" + std::to_string(r.links.size()) + "):\n";
        for (size_t i = 0; i < r.links.size() && i < 40; ++i) out += "- " + r.links[i].first + " - " + r.links[i].second + "\n";
      }
    } else {
      if (r.links.empty()) out += "(no links)";
      for (const auto& [text, href] : r.links) out += "- " + text + " - " + href + "\n";
    }
    return clip(out, options.maxChars);
  }

  *error = "unknown browser tool '" + tool + "'";
  return "";
}

}  // namespace browsertool

// ---------------------------------------------------------------- settings card

namespace app {
namespace {
std::string gTryUrl = "https://example.com";
std::string gTryResult;
bool gTrying = false;
}  // namespace

void App::drawBrowserToolCard(float width) {
  card("builtin-browser", [&] {
    browsertool::Options o = browsertool::optionsFromSettings();
    theme::pushBold();
    ImGui::TextUnformatted("Browser tool (built in)");
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("browser__fetch, render, links, screenshot, search");
    bool enabled = o.enabled;
    if (ImGui::Checkbox("Offer the browser tools to the model (with MCP tools on)", &enabled)) {
      settings().extra["browserTool"] = enabled;
      settings().save();
    }
    bool allowLocal = o.allowLocal;
    if (ImGui::Checkbox("Allow local and private-network hosts (localhost, 10.x, 192.168.x, .local)", &allowLocal)) {
      settings().extra["browserToolAllowLocal"] = allowLocal;
      settings().save();
    }
    int maxChars = static_cast<int>(o.maxChars);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderInt("Characters per result", &maxChars, 2000, 60000)) {
      settings().extra["browserToolMaxChars"] = maxChars;
      settings().save();
    }
    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputTextWithHint("##tryurl", "https://...", &gTryUrl);
    ImGui::SameLine();
    if (ImGui::Button(gTrying ? "Fetching..." : "Try fetch") && !gTrying) {
      gTrying = true;
      gTryResult.clear();
      const std::string url = gTryUrl;
      async::run([url, o] {
        std::string error;
        std::string text = browsertool::call("fetch", Json{{"url", url}}, o, &error);
        async::post([text, error] {
          gTrying = false;
          gTryResult = error.empty() ? text : "Error: " + error;
        });
      });
    }
    ImGui::SameLine();
    if (ImGui::Button(gTrying ? "Rendering..." : "Try render") && !gTrying) {
      gTrying = true;
      gTryResult.clear();
      const std::string url = gTryUrl;
      async::run([url, o] {
        std::string error;
        std::string text = browsertool::call("render", Json{{"url", url}}, o, &error);
        async::post([text, error] {
          gTrying = false;
          gTryResult = error.empty() ? text : "Error: " + error;
        });
      });
    }
    if (!gTryResult.empty()) {
      ImGui::BeginChild("##tryout", ImVec2(width - 40.0f, 160.0f), ImGuiChildFlags_Borders);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(gTryResult.substr(0, 3000).c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndChild();
    }
    widgets::dimText("Every call still asks for approval like any tool (\"Always allow\" remembers it). Only http(s); search uses DuckDuckGo's HTML page, no key.");
  }, width);
}

}  // namespace app
