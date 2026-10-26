// The built-in browser tool (#88): tools the model can call to fetch a page
// as Markdown, render it in a headless WKWebView (JavaScript runs) and read
// its text and links, take a screenshot, or search the web (DuckDuckGo's
// HTML endpoint, no key). It appears beside MCP and plugin tools under the
// "browser" server, goes through the same approval cards, and only ever
// speaks http(s) to public hosts unless the setting allows local ones.
#pragma once

#include <string>
#include <vector>

#include "agents/tool_loop.h"

namespace browsertool {

using Json = nlohmann::json;

struct Options {
  bool enabled = true;
  bool allowLocal = false;     // localhost / private ranges / .local
  size_t maxChars = 12000;     // per tool result
  int timeoutSec = 20;
};
Options optionsFromSettings();

constexpr const char* kServerId = "builtin:browser";
constexpr const char* kServerName = "browser";

std::vector<agents::ToolSpec> specs();
// Blocking; runs on the tool loop's worker. Returns the text for the model.
std::string call(const std::string& tool, const Json& args, const Options& options, std::string* error);

// ---- pieces, exposed for the self-test ------------------------------------
std::string normalizeUrl(const std::string& url);                 // trims, adds https:// when no scheme
std::string checkUrl(const std::string& url, bool allowLocal);    // "" when allowed, else the reason
std::string clip(const std::string& text, size_t maxChars);
struct SearchHit {
  std::string title, url, snippet;
};
std::vector<SearchHit> parseDuckDuckGo(const std::string& html);
std::string percentDecode(const std::string& s);

}  // namespace browsertool
