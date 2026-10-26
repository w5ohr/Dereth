// The HTML -> Markdown converter that URL capture uses (knowledge_tools.cpp),
// exposed for the browser tool.
#pragma once

#include <string>

namespace app {

// Readable Markdown for a page; `baseUrl` resolves relative links, `title`
// (optional) receives the <title>.
std::string htmlToMarkdownText(const std::string& html, const std::string& baseUrl, std::string* title);

}  // namespace app
